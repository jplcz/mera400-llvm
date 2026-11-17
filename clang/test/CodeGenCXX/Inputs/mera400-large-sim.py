"""Host-side execution of large memory model C++ code (clang -S output on stdin).

Runs "callfp" (a call through a function pointer), "callv" (two virtual
calls), "viapm" (a call through a pointer to a non-virtual and to a virtual
member function) on a small model of the MERA-400 that implements just the
instructions these functions use, and checks the results. In the large model
every pointer to code or to a vtable is a 32-bit BYTE address, the same unit as
an ELF symbol value, so a vtable entry is 4 bytes and an indirect call shifts
the address right by one to get the word address "rj" needs.

Layout used here: every instruction occupies one word from word address 0x100,
so the byte address of a code label is 2 * (0x100 + its instruction index); the
data objects follow from word address 0x1000. Two-word hi:lo pairs are stored
big endian (the high word at the lower address), as the back end does.
"""

import re
import sys

MASK = 0xFFFF
CODE = 0x100
DATA = 0x1000
STACK = 0x7000
RETURN = 0xFFFF
FLAG_Y, FLAG_X, FLAG_C, FLAG_E = "Y", "X", "C", "E"


def parse(text):
    labels, prog, data, section = {}, [], [], "text"
    for raw in text.splitlines():
        line = raw.split(";")[0].rstrip()
        s = line.strip()
        if not s:
            continue
        m = re.match(r"^(\S+):$", line)
        if m:
            if section == "text":
                labels[m.group(1)] = ("code", len(prog))
            else:
                labels[m.group(1)] = ("data", len(data))
            continue
        if s in (".data", ".text") or s.startswith(".section"):
            section = "text" if s == ".text" else "data"
            continue
        if s.startswith("."):
            if section == "data":
                d, _, v = s.partition("\t")
                v = v.strip()
                if d == ".long":
                    data.append(("long", v))
                elif d == ".short":
                    data.append(("short", v))
                elif d == ".p2align" or d == ".size" or d == ".type":
                    pass
            continue
        parts = s.split(None, 1)
        ops = [o.strip() for o in parts[1].split(",")] if len(parts) > 1 else []
        prog.append((parts[0], ops))
    return labels, prog, data


def build_memory(labels, data):
    # First pass: the word offset of every data item (a .long is two words).
    offsets, w = [], 0
    for kind, _ in data:
        offsets.append(w)
        w += 2 if kind == "long" else 1
    sym = {}
    for name, (kind, idx) in labels.items():
        if kind == "code":
            sym[name] = 2 * (CODE + idx)
        else:
            sym[name] = 2 * (DATA + (offsets[idx] if idx < len(offsets) else w))
    mem = {}
    for off, (kind, v) in zip(offsets, data):
        m = re.fullmatch(r"([\w.$]+)(?:\+(\d+))?", v)
        if m and not m.group(1).isdigit() and not v.startswith("0x"):
            value = sym[m.group(1)] + int(m.group(2) or 0)
        else:
            value = int(v, 0)
        if kind == "long":
            mem[DATA + off], mem[DATA + off + 1] = (value >> 16) & MASK, value & MASK
        else:
            mem[DATA + off] = value & MASK
    return sym, mem


class Machine:
    def __init__(self, labels, prog, mem):
        self.labels, self.prog, self.mem = labels, prog, mem
        self.r = [0] * 8
        self.flags = set()
        self.cmp = None

    def addr(self, tok):
        m = re.fullmatch(r"(?:(-?\d+)\+)?(r[0-7])", tok)
        assert m, tok
        return (int(m.group(1) or 0) + self.r[int(m.group(2)[1])]) & MASK

    def value(self, tok):
        if re.fullmatch(r"r[0-7]", tok):
            return self.r[int(tok[1])]
        return int(tok, 0) & MASK

    def setflag(self, f, on):
        (self.flags.add if on else self.flags.discard)(f)

    def run(self, entry):
        pc = CODE + self.labels[entry][1]
        self.r[4], self.r[7] = RETURN, STACK
        for _ in range(2000):
            if pc == RETURN:
                return
            op, a = self.prog[pc - CODE]
            pc += 1
            if op == "uj" and a[0] == "r4":
                pc = self.r[4]
                continue
            if op == "ujs":
                pc = CODE + self.labels[a[0]][1]
                continue
            if op == "jn":
                if "E" not in self.flags:
                    pc = CODE + self.labels[a[0]][1]
                continue
            r = int(a[0][1])
            if op in ("lw", "lwt"):
                self.r[r] = self.value(a[1])
            elif op == "tw":
                self.r[r] = self.mem.get(self.addr(a[1]), 0)
            elif op == "rw":
                self.mem[self.addr(a[1])] = self.r[r]
            elif op == "awt":
                self.r[r] = (self.r[r] + int(a[1])) & MASK
            elif op in ("aw", "ac"):
                n = self.value(a[1]) + (1 if op == "ac" and "C" in self.flags else 0)
                total = self.r[r] + n
                self.setflag("C", total > MASK)
                self.r[r] = total & MASK
            elif op == "nr":
                self.r[r] &= self.value(a[1])
            elif op == "cw":
                self.setflag("E", self.value(a[0]) == self.value(a[1]))
            elif op == "sxu":
                self.setflag("X", bool(self.r[r] & 0x8000))
            elif op in ("srz", "sry", "srx"):
                shift_in = {"z": 0, "y": int("Y" in self.flags), "x": int("X" in self.flags)}[op[2]]
                self.setflag("Y", bool(self.r[r] & 1))
                self.r[r] = (self.r[r] >> 1) | (shift_in << 15)
            elif op in ("slz",):
                self.setflag("Y", bool(self.r[r] & 0x8000))
                self.r[r] = (self.r[r] << 1) & MASK
            elif op == "rj" and a[1].startswith("r"):
                self.r[r] = pc
                pc = self.r[int(a[1][1])]
            else:
                raise AssertionError("instruction not modelled: " + op)
        raise AssertionError("runaway")


def main():
    labels, prog, data = parse(sys.stdin.read())
    sym, mem = build_memory(labels, data)
    # The linker would define _ZTV1B, _ZTV1D and d; the mangled names are fixed
    # by the C++ source of the test.
    # Calling convention: a 32-bit value takes two registers (high word first);
    # a 16-bit one takes one; a class object is passed by a word pointer to a
    # copy; "stack" arguments go at the stack pointer on entry.
    def call(entry, *args, stack=()):
        m = Machine(labels, prog, dict(mem))
        regs = []
        for x in args:
            regs += [(x[0] >> 16) & MASK, x[0] & MASK] if isinstance(x, tuple) else [x]
        for i, v in enumerate(regs):
            m.r[1 + i] = v
        for i, v in enumerate(stack):
            m.mem[STACK + i] = v
        m.run(entry)
        return m.r[1]

    def pm_call(b, ptr, adj, v):
        m = Machine(labels, prog, dict(mem))
        copy = 0x6000
        for i, x in enumerate((ptr >> 16, ptr, adj >> 16, adj)):
            m.mem[copy + i] = x & MASK
        m.r[1], m.r[2], m.r[3] = (b >> 16) & MASK, b & MASK, copy
        m.mem[STACK] = v
        m.run("_Z5viapmP1BMS_FiiEi")
        return m.r[1]

    # Hand made D and B objects (the constructors are not run): the vptr
    # points just after the two header words (offset-to-top, typeinfo) of the
    # class's vtable, 8 bytes, as the constructors would set it.
    def make_object(word, vtable):
        vp = sym[vtable] + 8
        mem[DATA + word], mem[DATA + word + 1], mem[DATA + word + 2] = (vp >> 16) & MASK, vp & MASK, 0
        return 2 * (DATA + word)

    obj_d = make_object(0x700, "_ZTV1D")
    b_obj = make_object(0x800, "_ZTV1B")

    checks = []
    # callfp(fp, 21): twice(21).
    fp = (mem[sym["fp"] // 2] << 16) | mem[sym["fp"] // 2 + 1]
    assert fp == sym["_Z5twicei"], "fp holds the byte address of twice"
    checks.append(("callfp", call("_Z6callfpPFiiEi", (fp,), 21), 42))
    # callv(b, 5): g(5) + f(); D: 10 + 7, B: 5 + 1.
    checks.append(("callv D", call("_Z5callvP1Bi", (obj_d,), 5), 17))
    checks.append(("callv B", call("_Z5callvP1Bi", (b_obj,), 5), 6))
    # viapm(b, pm, v): pm = &B::g (virtual, vtable offset 4, adj 1).
    pm_ptr = (mem[sym["pm"] // 2] << 16) | mem[sym["pm"] // 2 + 1]
    pm_adj = (mem[sym["pm"] // 2 + 2] << 16) | mem[sym["pm"] // 2 + 3]
    checks.append(("viapm virtual D", pm_call(obj_d, pm_ptr, pm_adj, 9), 18))
    checks.append(("viapm virtual B", pm_call(b_obj, pm_ptr, pm_adj, 9), 9))
    # A non-virtual member function pointer: the function's byte address, adj 0.
    checks.append(("viapm plain", pm_call(obj_d, sym["_ZN1B1gEi"], 0, 9), 9))
    for name, got, want in checks:
        assert got == want, "%s: got %d, want %d" % (name, got, want)
    print("large model calls ok (%d checks)" % len(checks))


main()
