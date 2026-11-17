"""Host-side simulation of word accesses through byte pointers (address space 1).

Reads the assembly llc produced for as1-access.ll on standard input, runs the
functions "ld16", "st32", "idx" and "byteoff" on a small model of the MERA-400
(only the instructions these sequences use) and compares what they read and
write with a byte-addressed reference: a byte pointer P addresses the word
P >> 1, and the high word of a 32-bit value is at the lower address. Pointers
at, below and above byte address 0x8000 are used (the whole 16-bit byte
address range).
"""

import random
import re
import sys

MASK = 0xFFFF
STACK = 0x0F00
RETURN = 0xFFFF


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


class Machine:
    def __init__(self, labels, prog, mem):
        self.labels, self.prog, self.mem = labels, prog, mem
        self.r = [0] * 8

    @staticmethod
    def reg(name):
        return int(name[1:])

    def addr(self, tok):
        # "rb" or "n+rb": the word at n + rb.
        m = re.fullmatch(r"(?:(-?\d+)\+)?(r[0-7])", tok)
        assert m, tok
        return (int(m.group(1) or 0) + self.r[self.reg(m.group(2))]) & MASK

    def value(self, tok):
        if re.fullmatch(r"r[0-7]", tok):
            return self.r[self.reg(tok)]
        return int(tok, 0) & MASK

    def run(self, entry):
        pc = self.labels[entry]
        self.r[4] = RETURN
        for _ in range(1000):
            op, a = self.prog[pc]
            pc += 1
            if op == "uj" and a[0] == "r4":
                return
            r = self.reg(a[0])
            if op in ("lw", "lwt"):
                self.r[r] = self.value(a[1])
            elif op == "tw":
                self.r[r] = self.mem[self.addr(a[1])]
            elif op == "rw":
                self.mem[self.addr(a[1])] = self.r[r]
            elif op == "awt":
                self.r[r] = (self.r[r] + int(a[1])) & MASK
            elif op == "aw":
                self.r[r] = (self.r[r] + self.value(a[1])) & MASK
            elif op == "srz":
                self.r[r] >>= 1
            elif op == "slz":
                self.r[r] = (self.r[r] << 1) & MASK
            else:
                raise AssertionError("instruction not modelled: " + op)
        raise AssertionError("runaway")


def main():
    labels, prog = parse(sys.stdin.read())
    rng = random.Random(400)
    checks = 0
    for byte_addr in (0x0100, 0x0102, 0x7FFE, 0x8000, 0x8002, 0xFFFC, 0xFFFE):
        w = byte_addr >> 1
        base = [rng.randrange(0x10000) for _ in range(0x10000)]

        m = Machine(labels, prog, list(base))
        m.r[1], m.r[7] = byte_addr, STACK
        m.run("ld16")
        assert m.r[1] == base[w], "ld16 %#x" % byte_addr
        assert m.mem == base, "ld16 modified memory"
        checks += 1

        # st32(p, x): x is in r2 (high word) and r3 (low word).
        m = Machine(labels, prog, list(base))
        m.r[1], m.r[2], m.r[3], m.r[7] = byte_addr, 0x1234, 0xABCD, STACK
        m.run("st32")
        want = list(base)
        want[w], want[(w + 1) & MASK] = 0x1234, 0xABCD
        assert m.mem == want, "st32 %#x" % byte_addr
        checks += 1

        # idx(p, i) reads the i16 at p + 2 * i.
        for i in (0, 1, 5, 0xFFFF):
            m = Machine(labels, prog, list(base))
            m.r[1], m.r[2], m.r[7] = byte_addr, i, STACK
            m.run("idx")
            assert m.r[1] == base[((byte_addr + 2 * i) & MASK) >> 1], "idx %#x %d" % (byte_addr, i)
            checks += 1

        m = Machine(labels, prog, list(base))
        m.r[1], m.r[7] = byte_addr, STACK
        m.run("byteoff")
        assert m.r[1] == base[((byte_addr + 6) & MASK) >> 1], "byteoff %#x" % byte_addr
        checks += 1
    print("as1 word access ok (%d checks)" % checks)


main()
