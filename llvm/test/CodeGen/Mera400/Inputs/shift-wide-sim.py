"""Host-side simulation of variable-count 32 and 64-bit shifts.

usage: shift-wide-sim.py {small|large} < assembly of shift-variable-wide.ll

Runs the functions on a small model of the MERA-400 (only the instructions
these sequences use). The calls to the library shift routines are performed
by Python stubs that follow the calling convention (the first three words in
r1..r3, then the stack words at r7, the 64-bit result through a pointer in
r1) and clobber r1..r4 like a real callee. The stack and the memory around it
are filled with junk. After every function the stub-computed result must be
right, and r5, r6 (callee saved), r7 and the return address in r4 must be
as on entry: the bug this test guards against was a stack temporary that
overwrote the register spill slots.
"""

import random
import re
import sys

MASK = 0xFFFF
RETURN = 0xFFFF
SENTINEL5, SENTINEL6 = 0x5155, 0x6166
FRAME_WORDS = 64  # generous upper bound of a frame of these functions


def parse(text):
    labels, prog = {}, []
    for raw in text.splitlines():
        line = raw.split(";")[0].rstrip()
        if not line.strip():
            continue
        m = re.match(r"^(\S+):$", line)
        if m:
            labels[m.group(1)] = len(prog)
            continue
        if line.startswith("\t."):
            continue
        parts = line.strip().split(None, 1)
        ops = [o.strip() for o in parts[1].split(",")] if len(parts) > 1 else []
        prog.append((parts[0], ops))
    return labels, prog


def join(words):
    v = 0
    for w in words:
        v = v << 16 | w
    return v


def split(v, n):
    return [(v >> (16 * (n - 1 - i))) & MASK for i in range(n)]


def signed(v, bits):
    return v - (1 << bits) if v >> (bits - 1) else v


def shift(kind, bits, a, n):
    if n >= bits:
        n = bits  # the builtins routines give 0 (or the sign) for such counts
    if kind == "shl":
        return (a << n) & ((1 << bits) - 1)
    if kind == "lshr":
        return a >> n
    return (signed(a, bits) >> n) & ((1 << bits) - 1)


class Machine:
    def __init__(self, labels, prog, mem):
        self.labels, self.prog, self.mem = labels, prog, mem
        self.r = [0] * 8
        self.carry = 0
        self.rng = random.Random(1)

    @staticmethod
    def reg(name):
        return int(name[1:])

    def addr(self, tok):
        m = re.fullmatch(r"(?:(-?\d+)\+)?(r[0-7])", tok)
        assert m, tok
        return (int(m.group(1) or 0) + self.r[self.reg(m.group(2))]) & MASK

    def value(self, tok):
        if re.fullmatch(r"r[0-7]", tok):
            return self.r[self.reg(tok)]
        return int(tok, 0) & MASK

    def stub(self, name):
        """A library shift routine; arguments as the calling convention says."""
        m = re.fullmatch(r"__(ashl|lshr|ashr)(si|di)3", name)
        assert m, "unexpected call " + name
        kind = {"ashl": "shl", "lshr": "lshr", "ashr": "ashr"}[m.group(1)]
        sp = self.r[7]
        if m.group(2) == "si":
            a = join(self.r[1:3])
            n = join(self.mem[sp:sp + 2])
            res = split(shift(kind, 32, a, n), 2)
            clobber = self.rng.randrange
            self.r[1:4] = res + [clobber(0x10000)]
        else:
            a = join([self.r[2], self.r[3], self.mem[sp], self.mem[sp + 1]])
            n = join(self.mem[sp + 2:sp + 4])
            res = split(shift(kind, 64, a, n), 4)
            for k in range(4):
                self.mem[(self.r[1] + k) & MASK] = res[k]
            self.r[1:4] = [self.rng.randrange(0x10000) for _ in range(3)]
        self.r[4] = self.rng.randrange(0x10000)

    def run(self, entry):
        pc = self.labels[entry]
        self.r[4] = RETURN
        for _ in range(5000):
            op, a = self.prog[pc]
            pc += 1
            if op == "uj" and a[0] == "r4":
                assert self.r[4] == RETURN, entry + ": return address lost"
                return
            if op == "rj":
                self.stub(a[1])
                continue
            r = self.reg(a[0])
            if op in ("lw", "lwt"):
                self.r[r] = self.value(a[1])
            elif op == "tw":
                self.r[r] = self.mem[self.addr(a[1])]
            elif op == "rw":
                self.mem[self.addr(a[1])] = self.r[r]
            elif op in ("aw", "awt", "ac"):
                v = self.value(a[1]) if op != "awt" else int(a[1]) & MASK
                total = self.r[r] + v + (self.carry if op == "ac" else 0)
                self.carry = total >> 16
                self.r[r] = total & MASK
            else:
                raise AssertionError("instruction not modelled: " + op)
        raise AssertionError("runaway")


def main():
    labels, prog = parse(sys.stdin.read())
    stack = 0x9000 if sys.argv[1] == "large" else 0x3000
    rng = random.Random(400)
    checks = 0
    counts = [0, 1, 3, 15, 16, 17, 31, 32, 33, 47, 63]
    for name in ("shl64", "lshr64", "ashr64", "shl32", "lshr32", "ashr32"):
        kind, bits = name[:-2], int(name[-2:])
        for n in counts:
            for _ in range(4):
                a = rng.getrandbits(bits)
                mem = [rng.randrange(0x10000) for _ in range(0x10000)]
                before = list(mem)
                m = Machine(labels, prog, mem)
                m.r[5], m.r[6], m.r[7] = SENTINEL5, SENTINEL6, stack
                words = split(a, bits // 16)
                result = 0x6000
                if bits == 64:
                    # r1 = result pointer, a in r2, r3 and stack slots 0, 1;
                    # the 16-bit count is the next stack slot.
                    m.r[1], m.r[2], m.r[3] = result, words[0], words[1]
                    m.mem[stack], m.mem[stack + 1] = words[2], words[3]
                    m.mem[stack + 2] = n
                else:
                    m.r[1], m.r[2], m.r[3] = words[0], words[1], n
                m.run(name)
                got = (join(m.mem[result:result + 4]) if bits == 64
                       else join(m.r[1:3]))
                want = shift(kind, bits, a, n)
                assert got == want, "%s(%#x, %d) = %#x, want %#x" % (
                    name, a, n, got, want)
                assert m.r[7] == stack and m.r[5] == SENTINEL5 \
                    and m.r[6] == SENTINEL6, name + ": registers not restored"
                # Only the function's frame (below the entry stack pointer),
                # its incoming stack arguments and the result buffer may differ.
                for w in range(0x10000):
                    if m.mem[w] != before[w]:
                        assert (stack - FRAME_WORDS <= w < stack + 4) or \
                            (result <= w < result + 4), \
                            "%s wrote outside its frame: %#x" % (name, w)
                checks += 1
    print("wide shifts ok (%d checks)" % checks)


main()
