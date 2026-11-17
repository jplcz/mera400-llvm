"""Host-side simulation of the large-model byte load and store sequences.

Reads the assembly that llc produced for the functions "ld" and "st" of
large-memory.ll on standard input, executes it on a small model of the MERA-400
word semantics (only the handful of instructions the sequences use) and
checks both octets of words below, at and above word 0x8000, which is where a
16-bit byte pointer (and lb/rb) stops reaching.
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
        self.y = 0
        self.z = False

    def reg(self, name):
        return int(name[1:])

    def imm(self, tok):
        return int(tok, 0) & MASK

    def src(self, tok):
        # A register, an immediate or a stack slot "n+r7".
        if re.fullmatch(r"r[0-7]", tok):
            return self.r[self.reg(tok)]
        m = re.fullmatch(r"(-?\d+)\+(r[0-7])", tok)
        if m:
            return self.mem[(int(m.group(1)) + self.r[self.reg(m.group(2))]) & MASK]
        return self.imm(tok)

    def run(self, entry):
        pc = self.labels[entry]
        self.r[4] = RETURN
        steps = 0
        while True:
            steps += 1
            assert steps < 1000, "runaway"
            op, a = self.prog[pc]
            pc += 1
            if op == "uj" and a[0] == "r4":
                return
            r = self.reg(a[0]) if a and re.fullmatch(r"r[0-7]", a[0]) else None
            if op == "lw":
                self.r[r] = self.src(a[1])
            elif op == "lwt":
                self.r[r] = self.imm(a[1])
            elif op == "tw":
                # "tw r, rb" loads the word addressed by rb (or a stack slot)
                self.r[r] = self.load(a[1])
            elif op == "rw":
                self.store(a[1], self.r[r])
            elif op == "awt":
                self.r[r] = (self.r[r] + int(a[1])) & MASK
            elif op == "nr":
                self.r[r] &= self.imm(a[1])
            elif op == "or":
                self.r[r] |= self.src(a[1])
            elif op == "cwt":
                self.z = self.r[r] == self.imm(a[1])
            elif op == "shc":
                n = int(a[1]) % 16
                v = self.r[r]
                # On the hardware a positive count rotates right.
                self.r[r] = ((v >> n) | (v << (16 - n))) & MASK
            elif op == "srz":
                self.y, self.r[r] = self.r[r] & 1, self.r[r] >> 1
            elif op == "sry":
                self.y, self.r[r] = self.r[r] & 1, (self.y << 15) | (self.r[r] >> 1)
            elif op == "slz":
                self.y, self.r[r] = self.r[r] >> 15, (self.r[r] << 1) & MASK
            elif op == "sly":
                self.y, self.r[r] = self.r[r] >> 15, ((self.r[r] << 1) | self.y) & MASK
            elif op == "jes":
                pc = self.labels[a[0]] if self.z else pc
            elif op == "jn":
                pc = self.labels[a[0]] if not self.z else pc
            elif op == "ujs":
                pc = self.labels[a[0]]
            else:
                raise AssertionError("instruction not modelled: " + op)

    def addr(self, tok):
        m = re.fullmatch(r"(?:(-?\d+)\+)?(r[0-7])", tok)
        assert m, tok
        return (int(m.group(1) or 0) + self.r[self.reg(m.group(2))]) & MASK

    def load(self, tok):
        return self.mem[self.addr(tok)]

    def store(self, tok, v):
        self.mem[self.addr(tok)] = v & MASK


def main():
    labels, prog = parse(sys.stdin.read())
    rng = random.Random(400)
    words = [0x0100, 0x0101, 0x7FFE, 0x7FFF, 0x8000, 0x8001, 0x8002, 0xFFFE, 0xFFFF]
    checks = 0
    for w in words:
        for octet in (0, 1):
            byte_addr = 2 * w + octet
            base = [rng.randrange(0x10000) for _ in range(0x10000)]

            m = Machine(labels, prog, list(base))
            m.r[1], m.r[2], m.r[7] = byte_addr >> 16, byte_addr & MASK, STACK
            m.run("ld")
            want = (base[w] >> 8) if octet == 0 else (base[w] & 0xFF)
            assert m.r[1] & 0xFF == want, "ld %#x: %#x != %#x" % (byte_addr, m.r[1] & 0xFF, want)
            assert m.mem == base, "ld modified memory"
            checks += 1

            for value in (0x00, 0x5A, 0xFF):
                m = Machine(labels, prog, list(base))
                m.r[1], m.r[2], m.r[7] = byte_addr >> 16, byte_addr & MASK, STACK
                # The upper octet of an i8 argument register is not defined.
                m.r[3] = (0xAB00 | value) & MASK
                m.r[5] = 0x1234
                m.run("st")
                keep = (base[w] & 0x00FF) if octet == 0 else (base[w] & 0xFF00)
                new = keep | (value << 8 if octet == 0 else value)
                expected = list(base)
                expected[w] = new
                expected[STACK - 1] = 0x1234  # the spilled r5
                assert m.mem == expected, "st %#x value %#x" % (byte_addr, value)
                assert m.r[7] == STACK and m.r[5] == 0x1234, "st clobbered r5/r7"
                checks += 1
    print("large byte access: %d checks passed" % checks)


main()
