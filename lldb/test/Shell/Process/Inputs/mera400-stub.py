#!/usr/bin/env python3
"""Minimal fake MERA-400 GDB-remote server used to test LLDB without a
machine or emulator.

It serves a fixed, stopped process (registers and memory below), runs LLDB
with the given command arguments against it and prints LLDB's output, so
FileCheck can verify it. The server lives only for one connection.

usage: mera400-stub.py [--reg NAME=HEX]... [--stop NAME=HEX]... <target.xml>
                       <lldb> [lldb args...]

--reg sets a register of the initial state, --stop sets registers when the
process is resumed (it then stops again at once, as if a breakpoint was hit).
"@PORT@" in the lldb arguments is replaced by the port of the server.
"""
import socket
import subprocess
import sys

# Registers in 'g' packet order: (name, size in bytes, value).
REGS = [
    ("r0", 2, 0x0000),
    ("r1", 2, 0x1234),
    ("r2", 2, 0x5678),
    ("r3", 2, 0x0003),
    ("r4", 2, 0x0000),
    ("r5", 2, 0x0005),
    ("r6", 2, 0x0006),
    ("r7", 2, 0x0100),  # stack pointer, byte address
    ("ic", 4, 0x00000200),  # byte address
    ("sr", 2, 0x0000),
]
STOP_REGS = {}
MEMORY = {0x100 + i: b for i, b in enumerate(bytes.fromhex("deadbeef01020304"))}


def set_reg(index, value):
    name, size, _ = REGS[index]
    REGS[index] = (name, size, value & ((1 << (8 * size)) - 1))


def reg_index(name):
    return [r[0] for r in REGS].index(name)


def checksum(data):
    return sum(data) & 0xFF


def recv_packet(conn):
    buf = b""
    while True:
        c = conn.recv(1)
        if not c:
            return None
        if c == b"$":
            buf = b""
        elif c == b"#":
            conn.recv(2)
            conn.sendall(b"+")
            return buf
        elif c in (b"+", b"-"):
            continue
        else:
            buf += c


def send_packet(conn, text):
    data = text.encode() if isinstance(text, str) else text
    conn.sendall(b"$" + data + b"#" + b"%02x" % checksum(data))


def handle(conn, xml):
    while True:
        pkt = recv_packet(conn)
        if pkt is None:
            return
        p = pkt.decode(errors="replace")
        if p == "?":
            send_packet(conn, "T05thread:1;")
        elif p.startswith("qSupported"):
            send_packet(conn, "PacketSize=4000;qXfer:features:read+")
        elif p.startswith("qXfer:features:read:target.xml:"):
            off, length = (int(x, 16) for x in p.split(":")[-1].split(","))
            chunk = xml[off : off + length]
            send_packet(conn, ("l" if off + length >= len(xml) else "m") + chunk)
        elif p == "qC":
            send_packet(conn, "QC1")
        elif p == "qAttached":
            send_packet(conn, "1")
        elif p == "qfThreadInfo":
            send_packet(conn, "m1")
        elif p == "qsThreadInfo":
            send_packet(conn, "l")
        elif p == "g":
            send_packet(conn, "".join("%0*x" % (2 * s, v) for _, s, v in REGS))
        elif p[0] == "p":
            _, s, v = REGS[int(p[1:], 16)]
            send_packet(conn, "%0*x" % (2 * s, v))
        elif p[0] == "m":
            addr, length = (int(x, 16) for x in p[1:].split(","))
            send_packet(conn, "".join("%02x" % MEMORY.get(addr + i, 0) for i in range(length)))
        elif p[0] == "P":
            index, value = p[1:].split("=")
            set_reg(int(index, 16), int(value, 16))
            send_packet(conn, "OK")
        elif p[0] == "M":
            head, data = p[1:].split(":")
            addr, length = (int(x, 16) for x in head.split(","))
            for i in range(length):
                MEMORY[addr + i] = int(data[2 * i : 2 * i + 2], 16)
            send_packet(conn, "OK")
        elif p.startswith(("Z0", "z0")):
            send_packet(conn, "OK")
        elif p == "vCont?":
            send_packet(conn, "vCont;c;s")
        elif p[0] in "cs" or p.startswith("vCont;"):
            for name, value in STOP_REGS.items():
                set_reg(reg_index(name), value)
            send_packet(conn, "T05thread:1;")
        elif p.startswith("H"):
            send_packet(conn, "OK")
        elif p == "D":
            send_packet(conn, "OK")
            return
        else:
            send_packet(conn, "")  # unsupported


def main():
    argv = sys.argv[1:]
    while argv and argv[0] in ("--reg", "--stop"):
        name, value = argv[1].split("=")
        if argv[0] == "--reg":
            set_reg(reg_index(name), int(value, 16))
        else:
            STOP_REGS[name] = int(value, 16)
        argv = argv[2:]
    xml = open(argv[0]).read()
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    port = server.getsockname()[1]
    args = [a.replace("@PORT@", str(port)) for a in argv[1:]]
    lldb = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    server.settimeout(30)
    try:
        conn, _ = server.accept()
        handle(conn, xml)
        conn.close()
    except socket.timeout:
        pass
    out, _ = lldb.communicate(timeout=60)
    sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
