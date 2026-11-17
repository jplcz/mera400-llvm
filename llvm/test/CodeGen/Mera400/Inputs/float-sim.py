"""Host-side simulation of the floating point sequences llc generates.

Usage: float-sim.py {plain|awp|const} [ir-file] < assembly

The native format (see Mera400FloatFormat.h): a 40-bit two's complement
mantissa m, read as a fraction, and an 8-bit two's complement exponent e; the
value is m * 2**e with 0.5 <= m < 1 (or -1 <= m < -0.5), in three words, the
mantissa first and the exponent in the low byte of the third word. A zero is
three zero words. This script has its own, independent implementation of the
format, so it also checks the constant conversion done by the compiler.

"plain" and "awp" run the functions of float.ll and float-awp.ll on a small
model of the MERA-400 (only the instructions these sequences use) and compare
the results with the exact arithmetic on the decoded values. The runtime
library functions (__m48_add and so on) and the AWP instructions (af, sf, mf,
df) are modelled by exact arithmetic; what is checked is how the compiler
passes the operands (R1:R2:R3 for the first one, the stack for the others, the
second operand of an AWP instruction in memory), where the result comes from
and that nothing else is clobbered.

"const" compares the constants in the code and in the data of float-const.ll
(the IR is the second argument) with the reference conversion.
"""

import re
import struct
import sys
from fractions import Fraction

MASK = 0xFFFF
STACK = 0x0F00
RETURN = 0xFFFF


def encode(value, rounding=True):
    """Converts a number to three native words, or raises OverflowError."""
    v = Fraction(value)
    if v == 0:
        return [0, 0, 0]
    # Normalise: 0.5 <= m < 1 for positive values, -1 <= m < -0.5 for negative.
    e = 0
    m = v
    while abs(m) >= 1:
        m /= 2
        e += 1
    while abs(m) < Fraction(1, 2):
        m *= 2
        e -= 1
    if m < 0 and m == Fraction(-1, 2):
        m, e = m * 2, e - 1
    scaled = m * 2**39
    mant = int(scaled // 1)
    dropped = scaled - mant
    # Round to nearest, ties to even; without rounding, the bits that do not
    # fit are dropped (which, for the two's complement mantissa, is a floor).
    if rounding and (dropped > Fraction(1, 2) or (dropped == Fraction(1, 2) and mant % 2)):
        mant += 1
    if mant == 2**39:
        mant, e = 2**38, e + 1
    if mant == -(2**38):
        mant, e = -(2**39), e - 1
    if e > 127:
        raise OverflowError
    if e < -128:
        return [0, 0, 0]
    mant &= (1 << 40) - 1
    return [mant >> 24, (mant >> 8) & MASK, ((mant & 0xFF) << 8) | (e & 0xFF)]


def decode(words):
    mant = (words[0] << 24) | (words[1] << 8) | (words[2] >> 8)
    if mant >= 1 << 39:
        mant -= 1 << 40
    exp = words[2] & 0xFF
    if exp >= 128:
        exp -= 256
    return Fraction(mant, 2**39) * Fraction(2) ** exp


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


def parse_data(text):
    """Maps every data label to the list of its .quad values."""
    data, label = {}, None
    for raw in text.splitlines():
        line = raw.split(";")[0].rstrip()
        m = re.match(r"^(\S+):$", line)
        if m:
            label = m.group(1)
            data.setdefault(label, [])
            continue
        m = re.match(r"^\t\.quad\t(-?\d+)", line)
        if m and label:
            data[label].append(int(m.group(1)) & ((1 << 64) - 1))
        elif re.match(r"^\t\.(section|data|bss|text)\b", line):
            label = None
    return data


class Machine:
    def __init__(self, labels, prog, rounding_helper=True):
        self.labels, self.prog = labels, prog
        self.mem = {}
        self.r = [0] * 8
        self.z = self.l = False

    def reg(self, name):
        return int(name[1:])

    def word(self, addr):
        return self.mem.get(addr & MASK, 0xDEAD)

    def imm(self, tok):
        return int(tok, 0) & MASK

    def addr(self, tok):
        m = re.fullmatch(r"(?:(-?\d+)\+)?(r[0-7])", tok)
        assert m, tok
        return (int(m.group(1) or 0) + self.r[self.reg(m.group(2))]) & MASK

    def src(self, tok):
        # A register or an immediate.
        if re.fullmatch(r"r[0-7]", tok):
            return self.r[self.reg(tok)]
        return self.imm(tok)

    def helper(self, name):
        a = decode(self.r[1:4])
        b = decode([self.word(self.r[7] + i) for i in range(3)])
        if name == "__m48_neg":
            res = -a
        elif name == "__m48_add":
            res = a + b
        elif name == "__m48_sub":
            res = a - b
        elif name == "__m48_mul":
            res = a * b
        elif name == "__m48_div":
            res = a / b
        elif name == "__m48_cmp":
            self.r[0] = 0x5555
            self.r[1] = (a > b) - (a < b) & MASK
            self.r[2] = self.r[3] = 0x6666
            return
        else:
            raise AssertionError("helper not modelled: " + name)
        self.r[0] = 0x5555
        self.r[1:4] = encode(res)

    def awp(self, op, tok):
        a = decode(self.r[1:4])
        b = decode([self.word(self.addr(tok) + i) for i in range(3)])
        res = {"af": a + b, "sf": a - b, "mf": a * b, "df": a / b if b else 0}[op]
        self.r[0] = 0x5555
        self.r[1:4] = encode(res)

    def run(self, entry):
        pc = self.labels[entry]
        self.r[4] = RETURN
        steps = 0
        while True:
            steps += 1
            assert steps < 2000, "runaway"
            op, a = self.prog[pc]
            pc += 1
            if op == "uj" and a[0] == "r4":
                assert self.r[4] == RETURN, "return address not restored"
                return
            r = self.reg(a[0]) if a and re.fullmatch(r"r[0-7]", a[0]) else None
            if op == "lw":
                self.r[r] = self.src(a[1])
            elif op == "lwt":
                self.r[r] = self.imm(a[1])
            elif op == "tw":
                self.r[r] = self.word(self.addr(a[1]))
            elif op == "rw":
                self.mem[self.addr(a[1])] = self.r[r]
            elif op == "awt":
                self.r[r] = (self.r[r] + int(a[1])) & MASK
            elif op == "nr":
                self.r[r] &= self.src(a[1])
            elif op == "xr":
                self.r[r] ^= self.src(a[1])
            elif op == "shc":
                n = int(a[1]) % 16
                v = self.r[r]
                self.r[r] = ((v << n) | (v >> (16 - n))) & MASK
            elif op == "cwt":
                v = self.r[r] - 0x10000 if self.r[r] & 0x8000 else self.r[r]
                self.z, self.l = v == int(a[1]), v < int(a[1])
            elif op == "jes":
                pc = self.labels[a[0]] if self.z else pc
            elif op == "jn":
                pc = self.labels[a[0]] if not self.z else pc
            elif op == "jls":
                pc = self.labels[a[0]] if self.l else pc
            elif op == "jgs":
                pc = self.labels[a[0]] if not self.l and not self.z else pc
            elif op == "ujs":
                pc = self.labels[a[0]]
            elif op == "rj":
                self.helper(a[1])
                self.r[4] = 0x7777
            elif op in ("af", "sf", "mf", "df"):
                self.awp(op, a[0])
            else:
                raise AssertionError("instruction not modelled: " + op)


SAMPLES = [
    Fraction(3, 2), Fraction(-2), Fraction(1, 10), Fraction(-1, 10),
    Fraction(40001, 400), Fraction(-300000), Fraction(0), Fraction(1, 100000),
    Fraction(12345678, 1000), Fraction(-1, 3), Fraction(1, 2), Fraction(-1, 2),
]


def call(labels, prog, name, args, stack_words=()):
    m = Machine(labels, prog)
    m.r[7] = STACK
    for i, w in enumerate(args):
        m.r[1 + i] = w
    for i, w in enumerate(stack_words):
        m.mem[STACK + i] = w
    m.run(name)
    assert m.r[7] == STACK, name + " did not restore r7"
    return m


def check_arithmetic(labels, prog, names):
    checks = 0
    ops = {
        "add": lambda a, b: a + b,
        "sub": lambda a, b: a - b,
        "mul": lambda a, b: a * b,
        "div": lambda a, b: a / b,
    }
    for x in SAMPLES:
        for y in SAMPLES:
            a, b = encode(x), encode(y)
            for name in ops:
                if name == "div" and y == 0:
                    continue
                want = encode(ops[name](decode(a), decode(b)))
                m = call(labels, prog, name, a, b)
                assert m.r[1:4] == want, "%s(%s, %s): %s != %s" % (name, x, y, m.r[1:4], want)
                checks += 1
            if "addc" in names:
                m = call(labels, prog, "addc", a)
                want = encode(decode(a) + Fraction(3, 2))
                assert m.r[1:4] == want, "addc(%s)" % x
                m = call(labels, prog, "loop", a, b)
                want = encode(decode(encode(decode(encode(decode(a) + decode(b))) + decode(b))) + decode(b))
                assert m.r[1:4] == want, "loop(%s, %s)" % (x, y)
                checks += 2
    return checks


def check_sign_and_compare(labels, prog):
    checks = 0
    for x in SAMPLES:
        a = encode(x)
        for name, want in (("neg", -decode(a)), ("abs", abs(decode(a)))):
            m = call(labels, prog, name, a)
            assert m.r[1:4] == encode(want), "%s(%s): %s" % (name, x, m.r[1:4])
            checks += 1
        for y in SAMPLES:
            b = encode(y)
            m = call(labels, prog, "cs", a, b)
            # The sign of zero is positive in the native format.
            want = abs(x) if y >= 0 else -abs(x)
            assert m.r[1:4] == encode(want), "cs(%s, %s): %s" % (x, y, m.r[1:4])
            m = call(labels, prog, "maxf", a, b)
            assert m.r[1:4] == encode(max(x, y)), "maxf(%s, %s)" % (x, y)
            m = call(labels, prog, "lt", a, b)
            assert m.r[1] == int(decode(a) < decode(b)), "lt(%s, %s)" % (x, y)
            for c in (0, 1):
                m = call(labels, prog, "sel", [c], a + b)
                assert m.r[1:4] == (a if c else b), "sel(%d, %s, %s)" % (c, x, y)
            checks += 4
    return checks


def ir_double(text):
    """The value of a double constant as LLVM IR spells it (decimal or hex)."""
    if text.startswith("0x"):
        return struct.unpack(">d", bytes.fromhex(text[2:].rjust(16, "0")))[0]
    return float(text)


def check_constants(labels, prog, asm, ir):
    data = parse_data(asm)
    checks = 0
    for m in re.finditer(r"define double @(c\w+)\(\) \{ ret double (\S+) \}", ir):
        name, text = m.groups()
        want = encode(Fraction(ir_double(text)))
        got = call(labels, prog, name, []).r[1:4]
        assert got == want, "%s: %s != %s" % (name, got, want)
        checks += 1
    for line in re.findall(r"^@g\w+ = .*$", ir, re.M):
        name = re.match(r"@(\w+)", line).group(1)
        words = [Fraction(ir_double(v)) for v in re.findall(r"double (0x[0-9A-Fa-f]+|-?[0-9.e+-]+)", line)]
        quads = data[name]
        assert len(quads) == len(words), name
        for q, w in zip(quads, words):
            want = encode(w)
            got = [(q >> 48) & MASK, (q >> 32) & MASK, (q >> 16) & MASK]
            assert got == want and q & MASK == 0, "%s: %s != %s" % (name, got, want)
            checks += 1
    return checks


def main():
    mode = sys.argv[1]
    asm = sys.stdin.read()
    labels, prog = parse(asm)
    if mode == "const":
        ir = open(sys.argv[2]).read()
        print("float constants: %d checks passed" % check_constants(labels, prog, asm, ir))
        return
    checks = check_arithmetic(labels, prog, labels)
    if mode == "plain":
        checks += check_sign_and_compare(labels, prog)
    print("float %s: %d checks passed" % (mode, checks))


main()
