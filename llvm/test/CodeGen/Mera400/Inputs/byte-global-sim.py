"""Host-side simulation of byte sized accesses to globals (byte-global.ll).

usage: byte-global-sim.py {small|large} < assembly of byte-global.ll

The globals are placed at word addresses b = 0x100 (4 bytes), f = 0x200 (a
bool byte) and w = 0x300 (a word). Memory starts as random words, every
function runs on a small model of the MERA-400 (only the instructions these
sequences use), and the result has to be exactly the C meaning on a big
endian word machine: the byte at an even byte address is the HIGH octet of its
word. Anything else in memory (other than the function's own frame and the
register spill slots just below the stack pointer) must be unchanged, which
also catches a byte store that went to a neighbouring word.
"""

import random
import re
import sys

MASK = 0xFFFF
RETURN = 0xFFFF
SYMBOLS = {"b": 0x100, "f": 0x200, "w": 0x300}


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
        if line.startswith("\t.") or line.startswith("\t\t."):
            continue
        parts = line.strip().split(None, 1)
        ops = [o.strip() for o in parts[1].split(",")] if len(parts) > 1 else []
        prog.append((parts[0], ops))
    return labels, prog


class Machine:
    def __init__(self, labels, prog, mem, rng):
        self.labels, self.prog, self.mem = labels, prog, mem
        # Registers start as junk: nothing may depend on their initial value.
        self.r = [rng.randrange(0x10000) for _ in range(8)]
        self.y = rng.randrange(2)
        self.carry = rng.randrange(2)
        self.z = False

    @staticmethod
    def reg(name):
        return int(name[1:])

    def value(self, tok):
        """The value of an operand used as data or as an address."""
        if re.fullmatch(r"r[0-7]", tok):
            return self.r[self.reg(tok)]
        m = re.fullmatch(r"(-?\d+)\+(r[0-7])", tok)
        if m:
            return (int(m.group(1)) + self.r[self.reg(m.group(2))]) & MASK
        m = re.fullmatch(r"([a-z]+)(?:\+(\d+))?", tok)
        if m and m.group(1) in SYMBOLS:
            return (SYMBOLS[m.group(1)] + int(m.group(2) or 0)) & MASK
        return int(tok, 0) & MASK

    def run(self, entry):
        pc = self.labels[entry]
        self.r[4] = RETURN
        for _ in range(2000):
            op, a = self.prog[pc]
            pc += 1
            if op == "uj" and a[0] == "r4":
                assert self.r[4] == RETURN, entry + ": return address lost"
                return
            if op in ("jn", "jes", "ujs"):
                if op == "ujs" or (op == "jes") == self.z:
                    pc = self.labels[a[0]]
                continue
            r = self.reg(a[0])
            if op in ("lw", "lwt"):
                self.r[r] = self.value(a[1])
            elif op == "tw":
                self.r[r] = self.mem[self.value(a[1])]
            elif op == "rw":
                self.mem[self.value(a[1])] = self.r[r]
            elif op == "lb":
                # Byte address; the even byte is the high octet. Keeps the
                # destination's high octet, so only the low one is replaced.
                ba = self.value(a[1])
                w = self.mem[ba >> 1]
                v = (w >> 8) if ba % 2 == 0 else (w & 0xFF)
                self.r[r] = (self.r[r] & 0xFF00) | v
            elif op == "rb":
                ba = self.value(a[1])
                w, v = self.mem[ba >> 1], self.r[r] & 0xFF
                self.mem[ba >> 1] = ((w & 0x00FF) | v << 8) if ba % 2 == 0 \
                    else ((w & 0xFF00) | v)
            elif op == "awt":
                self.r[r] = (self.r[r] + int(a[1])) & MASK
            elif op in ("aw", "ac"):
                t = self.r[r] + self.value(a[1]) + (self.carry if op == "ac" else 0)
                self.carry, self.r[r] = t >> 16, t & MASK
            elif op == "nr":
                self.r[r] &= self.value(a[1])
            elif op == "or":
                self.r[r] |= self.value(a[1])
            elif op == "cwt":
                self.z = self.r[r] == self.value(a[1])
            elif op == "shc":
                n = int(a[1]) % 16
                v = self.r[r]
                self.r[r] = ((v >> n) | (v << (16 - n))) & MASK
            elif op == "slz":
                self.y, self.r[r] = self.r[r] >> 15, (self.r[r] << 1) & MASK
            elif op == "sly":
                self.y, self.r[r] = self.r[r] >> 15, \
                    ((self.r[r] << 1) | self.y) & MASK
            elif op == "srz":
                self.y, self.r[r] = self.r[r] & 1, self.r[r] >> 1
            elif op == "sry":
                self.y, self.r[r] = self.r[r] & 1, \
                    (self.y << 15) | (self.r[r] >> 1)
            else:
                raise AssertionError("instruction not modelled: " + op)
        raise AssertionError("runaway")


def octets(mem, sym, n):
    """The n bytes of the object at a word address, as a list."""
    out = []
    for i in range(n):
        w = mem[SYMBOLS[sym] + i // 2]
        out.append(w >> 8 if i % 2 == 0 else w & 0xFF)
    return out


def put(mem, sym, i, v):
    a = SYMBOLS[sym] + i // 2
    mem[a] = ((mem[a] & 0x00FF) | v << 8) if i % 2 == 0 else \
        ((mem[a] & 0xFF00) | v)


def main():
    labels, prog = parse(sys.stdin.read())
    stack = 0x9000 if sys.argv[1] == "large" else 0x3000
    rng = random.Random(400)
    checks = 0

    def call(name, setup=None):
        mem = [rng.randrange(0x10000) for _ in range(0x10000)]
        before = list(mem)
        m = Machine(labels, prog, mem, rng)
        m.r[7] = stack
        if setup:
            setup(m)
        m.run(name)
        assert m.r[7] == stack, name + ": r7 not restored"
        expected = list(before)
        return m, before, expected

    def done(name, m, expected):
        # Only the frame just below the entry stack pointer may differ.
        for w in range(0x10000):
            if m.mem[w] != expected[w]:
                assert stack - 8 <= w < stack, "%s: word %#x wrong: %#x, " \
                    "want %#x" % (name, w, m.mem[w], expected[w])

    for _ in range(8):
        for name, idx, val in (("set_b0", 0, 5), ("set_b1", 1, 7),
                               ("set_b3", 3, 9)):
            m, before, expected = call(name)
            put(expected, "b", idx, val)
            done(name, m, expected)
            checks += 1
        m, before, expected = call("get_b2")
        assert m.r[1] & 0xFF == octets(before, "b", 4)[2], "get_b2"
        done("get_b2", m, before)
        for name, val in (("set_f", 1), ("clear_f", 0)):
            m, before, expected = call(name)
            put(expected, "f", 0, val)
            done(name, m, expected)
            checks += 1
        m, before, expected = call("get_f")
        assert m.r[1] & 1 == octets(before, "f", 1)[0] & 1, "get_f"
        done("get_f", m, before)
        for name, idx in (("set_w_high", 0), ("set_w_low", 1)):
            x = rng.randrange(0x100)
            m, before, expected = call(name, lambda m: m.r.__setitem__(1, x))
            put(expected, "w", idx, x)
            done(name, m, expected)
            checks += 1
        checks += 2
    print("byte globals ok (%d checks)" % checks)


main()
