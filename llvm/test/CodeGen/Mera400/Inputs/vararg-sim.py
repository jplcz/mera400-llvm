"""Host-side simulation of variadic calls and va_arg sequences.

usage: vararg-sim.py {small|large} < assembly of vararg.ll

Runs the functions of vararg.ll on a small model of the MERA-400 (only the
instructions these sequences use) with the stack arguments laid out as the
convention says (slot k at k + R7 on entry; the memory around is filled with
junk, so a read of a wrong slot fails) and checks that
  * the callers put the named arguments in registers and every variadic
    argument in consecutive stack words (a call to "take" is a stub that
    records the registers and the words at R7),
  * va_start produces the byte address of the first variadic slot,
  * va_arg reads i16, i32 and double (three words, never the fourth) in order
    and advances correctly, and va_copy gives an independent list.
In the large model a pointer is two registers (high word first) and the word
address of the stack may be at or above 0x8000, so the byte address needs 17
bits.
"""

import random
import re
import sys

MASK = 0xFFFF
RETURN = 0xFFFF
GD = 0x4000  # word address given to the global "gd"
SYMBOLS = {"gd": GD}


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
        self.carry = 0
        self.y = 0
        self.calls = []

    @staticmethod
    def reg(name):
        return int(name[1:])

    def addr(self, tok):
        # "rb", "n+rb", "symbol" or "symbol+n".
        m = re.fullmatch(r"(?:(-?\d+)\+)?(r[0-7])", tok)
        if m:
            return (int(m.group(1) or 0) + self.r[self.reg(m.group(2))]) & MASK
        m = re.fullmatch(r"([A-Za-z_][\w.]*)(?:\+(\d+))?", tok)
        assert m and m.group(1) in SYMBOLS, tok
        return SYMBOLS[m.group(1)] + int(m.group(2) or 0)

    def value(self, tok):
        if re.fullmatch(r"r[0-7]", tok):
            return self.r[self.reg(tok)]
        return int(tok, 0) & MASK

    def run(self, entry):
        pc = self.labels[entry]
        self.r[4] = RETURN
        for _ in range(5000):
            op, a = self.prog[pc]
            pc += 1
            if op == "uj" and a[0] == "r4":
                return
            if op == "rj":
                # A call: only record the arguments, the callee is not run.
                self.calls.append((a[1], list(self.r[1:4]),
                                   [self.mem[(self.r[7] + k) & MASK] for k in range(8)]))
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
            elif op == "nr":
                self.r[r] &= self.value(a[1])
            elif op == "shc":
                n = int(a[1]) % 16
                v = self.r[r]
                self.r[r] = ((v >> n) | (v << (16 - n))) & MASK
            elif op == "srz":
                self.y, self.r[r] = self.r[r] & 1, self.r[r] >> 1
            elif op == "sry":
                self.y, self.r[r] = self.r[r] & 1, (self.y << 15) | (self.r[r] >> 1)
            elif op == "slz":
                self.y, self.r[r] = self.r[r] >> 15, (self.r[r] << 1) & MASK
            elif op == "sly":
                self.y, self.r[r] = self.r[r] >> 15, ((self.r[r] << 1) | self.y) & MASK
            else:
                raise AssertionError("instruction not modelled: " + op)
        raise AssertionError("runaway")


def main():
    large = sys.argv[1] == "large"
    labels, prog = parse(sys.stdin.read())
    rng = random.Random(400)
    checks = 0

    def call(name, regs, slots, stack):
        """Runs name with registers r1.. and stack slots 0.. (junk after)."""
        nonlocal checks
        mem = [rng.randrange(0x10000) for _ in range(0x10000)]
        for k, v in enumerate(slots):
            mem[stack + k] = v
        m = Machine(labels, prog, mem)
        for i, v in enumerate(regs):
            m.r[1 + i] = v
        m.r[7] = stack
        m.run(name)
        assert m.r[7] == stack, name + ": stack pointer not restored"
        checks += 1
        return m

    def pointer(m):
        return (m.r[1] << 16 | m.r[2]) if large else m.r[1]

    def check(cond, what):
        nonlocal checks
        checks += 1
        assert cond, what

    # The callers.
    m = call("caller", [], [], 0x0F00)
    name, regs, words = m.calls[0]
    check(name == "take" and regs[0] == 1 and words[:6] == [2, 0, 3, 0x4000, 0, 1],
          "caller: r1=%d stack=%s" % (regs[0], words))
    m = call("caller_named", [9], [], 0x0F00)
    name, regs, words = m.calls[0]
    check(name == "take2" and regs == [9, 2, 3] and words[:6] == [4, 5, 0, 0, 0, 6],
          "caller_named: %s %s" % (regs, words))
    m = call("caller_none", [], [], 0x0F00)
    check(m.calls[0][1][0] == 1, "caller_none")

    # The callees. The first variadic slot is at the stack pointer on entry.
    for stack in ((0x0F00, 0x9000) if large else (0x0F00, 0x3000)):
        m = call("start", [7], [1, 2, 3], stack)
        check(pointer(m) == 2 * stack, "start %#x: %#x" % (stack, pointer(m)))
        m = call("start_named", [1, 2, 3], [4, 5, 6], stack)
        check(pointer(m) == 2 * (stack + 2), "start_named %#x: %#x" % (stack, pointer(m)))
        m = call("sum3", [0], [10, 20, 30, 999], stack)
        check(m.r[1] == 60, "sum3 %#x: %d" % (stack, m.r[1]))
        m = call("long2", [0], [0x0001, 0x0002, 0x0003, 0x8004, 0xBEEF], stack)
        got = m.r[1] << 16 | m.r[2]
        check(got == 0x00010002 + 0x00038004,
              "long2 %#x: %#x" % (stack, got))
        m = call("dbl", [0], [0x4000, 0x1234, 0x0005, 0xDEAD, 0xBEEF], stack)
        got = m.mem[GD:GD + 4]
        check(got == [0x4000, 0x1234, 0x0005, 0], "dbl %#x: %s" % (stack, got))
        m = call("sec", [0], [7, 11, 13], stack)
        check(m.r[1] == 22, "sec %#x: %d" % (stack, m.r[1]))
        m = call("as1_list", [0], [5, 6], stack)
        check(m.r[1] == 5, "as1_list %#x: %d" % (stack, m.r[1]))
        m = call("bytearg", [0], [0x1234, 6], stack)
        check(m.r[1] & 0xFF == 0x34, "bytearg %#x: %#x" % (stack, m.r[1]))
    print("varargs ok (%d checks)" % checks)


main()
