"""Host-side simulation of the inline memcpy, memmove and memset sequences.

Reads the assembly llc produced for memintrin.ll on standard input and runs
the functions that expand a memory intrinsic inline ("cpy6", "cpy4_bytes",
"cpy5_aligned", "move4", "set6", "set5") on a small model of the MERA-400
(only the instructions these sequences use). Each result is compared with the
C meaning of the operation on a byte-addressed reference memory: exactly the
destination bytes change, with the bytes of the source (read before any write
for memmove) or the fill value. Word pointers are word addresses, byte
pointers are byte addresses (word address * 2, plus 0 for the high octet).
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
        m = re.fullmatch(r"(?:(-?\d+)\+)?(r[0-7])", tok)
        assert m, tok
        return (int(m.group(1) or 0) + self.r[self.reg(m.group(2))]) & MASK

    def value(self, tok):
        if re.fullmatch(r"r[0-7]", tok):
            return self.r[self.reg(tok)]
        return int(tok, 0) & MASK

    def get_byte(self, b):
        w = self.mem[(b & MASK) >> 1]
        return (w >> 8) if not b & 1 else w & 0xFF

    def put_byte(self, b, v):
        i = (b & MASK) >> 1
        w = self.mem[i]
        self.mem[i] = ((v & 0xFF) << 8 | (w & 0xFF)) if not b & 1 else ((w & 0xFF00) | (v & 0xFF))

    def run(self, entry):
        pc = self.labels[entry]
        self.r[4] = RETURN
        for _ in range(2000):
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
            elif op == "lb":
                self.r[r] = (self.r[r] & 0xFF00) | self.get_byte(self.addr(a[1]))
            elif op == "rb":
                self.put_byte(self.addr(a[1]), self.r[r])
            elif op == "awt":
                self.r[r] = (self.r[r] + int(a[1])) & MASK
            elif op == "nr":
                self.r[r] &= self.value(a[1])
            elif op == "or":
                self.r[r] |= self.value(a[1])
            elif op == "shc":
                v = self.r[r]
                self.r[r] = ((v >> 8) | (v << 8)) & MASK
            elif op == "srz":
                self.r[r] >>= 1
            elif op == "slz":
                self.r[r] = (self.r[r] << 1) & MASK
            else:
                raise AssertionError("instruction not modelled: " + op)
        raise AssertionError("runaway")


def check(labels, prog, base, name, args, effect):
    """Runs name(args) and compares with effect(reference_bytes)."""
    m = Machine(labels, prog, list(base))
    for i, v in enumerate(args):
        m.r[1 + i] = v
    m.r[7] = STACK
    m.run(name)
    ref = Machine(labels, prog, list(base))
    effect(ref)
    # The function may spill registers just below the stack pointer.
    lo = STACK - 16
    assert m.mem[:lo] == ref.mem[:lo] and m.mem[STACK:] == ref.mem[STACK:], name + " " + str(args)
    assert m.r[7] == STACK, name + " moved the stack pointer"


def main():
    labels, prog = parse(sys.stdin.read())
    rng = random.Random(400)
    base = [rng.randrange(0x10000) for _ in range(0x10000)]
    checks = 0

    def run(name, args, effect):
        nonlocal checks
        check(labels, prog, base, name, args, effect)
        checks += 1

    # Disjoint objects, and objects at both ends of the 16-bit address space.
    for d, s in ((0x200, 0x300), (0x300, 0x200), (0x7FF0, 0x0100), (0x0100, 0x7FF0)):
        def copy_words(ref, d=d, s=s, n=3):
            for i in range(n):
                ref.mem[d + i] = ref.mem[s + i]
        run("cpy6", [d, s], copy_words)

        for bd, bs in ((2 * d, 2 * s), (2 * d + 1, 2 * s + 1), (2 * d, 2 * s + 1)):
            def copy_bytes(ref, bd=bd, bs=bs, n=4):
                data = [ref.get_byte(bs + i) for i in range(n)]
                for i in range(n):
                    ref.put_byte(bd + i, data[i])
            run("cpy4_bytes", [bd, bs], copy_bytes)

        def copy5(ref, d=d, s=s):
            data = [ref.get_byte(2 * s + i) for i in range(5)]
            for i in range(5):
                ref.put_byte(2 * d + i, data[i])
        run("cpy5_aligned", [2 * d, 2 * s], copy5)

    # memmove of 4 words that overlap in both directions.
    for d, s in ((0x400, 0x401), (0x401, 0x400), (0x400, 0x402), (0x402, 0x400)):
        def move(ref, d=d, s=s):
            data = [ref.mem[s + i] for i in range(2)]
            for i in range(2):
                ref.mem[d + i] = data[i]
        run("move4", [d, s], move)

    def fill6(ref):
        for i in range(3):
            ref.mem[0x500 + i] = 0x4141
    run("set6", [0x500], fill6)

    for c in (0x00, 0x5A, 0xFF, 0x1A5):
        def fill5(ref, c=c):
            for i in range(5):
                ref.put_byte(2 * 0x500 + i, c)
        # set5 takes the word address of the destination and the fill value.
        run("set5", [0x500, c], fill5)

    print("memory intrinsics ok (%d checks)" % checks)


main()
