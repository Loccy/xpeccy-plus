#!/usr/bin/env python3
"""DeZog's protocol (DZRP 2.2) against the emulator, with no VS Code.

Runs the staged build headless (--bench, a frame count it never reaches) with
--dezog-port, speaks DZRP to it the way DeZog does, and checks the replies: INIT,
the supported commands, registers and slots, memory by address and by bank, a
breakpoint, steps by temporary breakpoints (into and over a CALL), a write
watchpoint, a pause, a bank-bound breakpoint, a state saved and put back, the
DZRP 2.0 commands DeZog 3.7 sends, the user's own breakpoint (--bp) reported in words,
and the Next's own commands on the Next.

    python tests/dzrp/dzrp_test.py                  # zx48 zx128 zxnext pent1024 zxplus3
    python tests/dzrp/dzrp_test.py --app build/dist/<folder> zx128
"""

import argparse
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXE = "xpeccy-plus.exe" if sys.platform == "win32" else "xpeccy-plus"

CMD_INIT, CMD_CLOSE, CMD_GET_REGISTERS, CMD_SET_REGISTER = 1, 2, 3, 4
CMD_CONTINUE, CMD_PAUSE, CMD_READ_MEM, CMD_WRITE_MEM, CMD_SET_SLOT = 6, 7, 8, 9, 10
CMD_GET_NEXTREG = 11
CMD_GET_SPRITES_PALETTE, CMD_GET_SPRITES_CLIP, CMD_GET_SPRITES, CMD_GET_SPRITE_PATTERNS = 16, 17, 18, 19
CMD_INTERRUPT_ON_OFF, CMD_GET_SUPPORTED = 23, 24
CMD_READ_BANK_MEM, CMD_WRITE_BANK_MEM, CMD_SET_NEXTREGS, CMD_READ_MEM_BLOCKS = 25, 26, 27, 28
CMD_ADD_BREAKPOINT, CMD_REMOVE_BREAKPOINT, CMD_ADD_WATCHPOINT, CMD_REMOVE_WATCHPOINT = 40, 41, 42, 43
CMD_READ_STATE, CMD_WRITE_STATE = 50, 51
NTF_PAUSE = 1
REG_PC, REG_SP, REG_A = 0, 1, 15

# machine: (DZRP machine type, slots)
MACHINES = {
    "zx48": (2, 2),
    "zx128": (3, 4),
    "zxnext": (4, 8),
    "pent1024": (3, 4),
    "zxplus3": (3, 4),
}

# at #8000: LD A,1 / CALL #8010 / LD (#9000),A / JR $ ... #8010: INC A / RET
PROG = bytes([0x3E, 0x01, 0xCD, 0x10, 0x80, 0x32, 0x00, 0x90, 0x18, 0xFE,
              0, 0, 0, 0, 0, 0, 0x3C, 0xC9])


class Fail(Exception):
    pass


def check(cond, what):
    if not cond:
        raise Fail(what)


class Dzrp:
    def __init__(self, port):
        self.sock = None
        t0 = time.monotonic()
        while time.monotonic() - t0 < 30:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
                break
            except OSError:
                time.sleep(0.2)
        check(self.sock, f"no server on port {port}")
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.seq = 0
        self.ntfs = []

    def recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            part = self.sock.recv(n - len(buf))
            check(part, "connection closed")
            buf += part
        return buf

    def recv_msg(self):
        (length,) = struct.unpack("<I", self.recv_exact(4))
        return self.recv_exact(length)

    def cmd(self, cid, payload=b""):
        self.seq = self.seq % 15 + 1
        self.sock.sendall(struct.pack("<IBB", len(payload), self.seq, cid) + payload)
        while True:
            msg = self.recv_msg()
            if msg[0] == 0:			# a notification
                self.ntfs.append(msg)
                continue
            check(msg[0] == self.seq, f"command {cid}: seq {msg[0]}, sent {self.seq}")
            return msg[1:]

    def pause_ntf(self, timeout=10):
        """The NTF_PAUSE after a CONTINUE: (reason, address, bank+1, text)."""
        self.sock.settimeout(timeout)
        try:
            msg = self.ntfs.pop(0) if self.ntfs else self.recv_msg()
        except socket.timeout:
            raise Fail("no NTF_PAUSE")
        finally:
            self.sock.settimeout(5)
        check(msg[0] == 0 and msg[1] == NTF_PAUSE, f"not a pause notification: {msg[:2].hex()}")
        text = msg[6:].split(b"\0")[0].decode("latin-1")
        return msg[2], msg[3] | (msg[4] << 8), msg[5], text

    def regs(self):
        r = self.cmd(CMD_GET_REGISTERS)
        names = ["PC", "SP", "AF", "BC", "DE", "HL", "IX", "IY", "AF'", "BC'", "DE'", "HL'"]
        d = {n: struct.unpack_from("<H", r, 2 * i)[0] for i, n in enumerate(names)}
        d["R"], d["I"], d["IM"] = r[24], r[25], r[26]
        d["slots"] = list(r[29:29 + r[28]])
        return d

    def set_reg(self, reg, val):
        self.cmd(CMD_SET_REGISTER, struct.pack("<BH", reg, val))

    def write_mem(self, adr, data):
        self.cmd(CMD_WRITE_MEM, struct.pack("<BH", 0, adr) + data)

    def read_mem(self, adr, size):
        return self.cmd(CMD_READ_MEM_BLOCKS, struct.pack("<IHH", size + 1, adr, size))

    def cont(self, bp1=None, bp2=None):
        self.cmd(CMD_CONTINUE, struct.pack("<BHBHBHH", bp1 is not None, bp1 or 0,
                                           bp2 is not None, bp2 or 0, 0, 0, 0))


def bank_of(model, slots, adr):
    """The bank and its offset for a cpu address, as DeZog's model numbers them."""
    if model == 4:
        return slots[adr >> 13], adr & 0x1FFF
    if model == 3:
        return slots[adr >> 14], adr & 0x3FFF
    return (0, adr) if adr < 0x4000 else (1, adr - 0x4000)


def run_machine(d, machine):
    model, nslots = MACHINES[machine]
    r = d.cmd(CMD_INIT, bytes([2, 2, 0]) + b"dzrp_test\0")
    check(r[0] == 0, f"INIT error {r[0]}")
    check(tuple(r[1:4]) == (2, 2, 0), f"DZRP version {tuple(r[1:4])}")
    check(r[4] == model, f"machine type {r[4]}, expected {model}")
    check(r[5:].startswith(b"Xpeccy+"), f"program name {r[5:]!r}")

    bits = d.cmd(CMD_GET_SUPPORTED)
    have = {c for c in range(len(bits) * 8) if bits[c >> 3] & (1 << (c & 7))}
    need = {CMD_INIT, CMD_CLOSE, CMD_GET_REGISTERS, CMD_SET_REGISTER, CMD_CONTINUE, CMD_PAUSE,
            CMD_WRITE_MEM, CMD_GET_SUPPORTED, CMD_READ_MEM_BLOCKS, CMD_ADD_BREAKPOINT,
            CMD_REMOVE_BREAKPOINT, CMD_ADD_WATCHPOINT, CMD_REMOVE_WATCHPOINT,
            CMD_READ_BANK_MEM, CMD_WRITE_BANK_MEM, CMD_READ_STATE, CMD_WRITE_STATE}
    check(need <= have, f"not advertised: {sorted(need - have)}")
    nxt = {CMD_GET_NEXTREG, CMD_GET_SPRITES_PALETTE, CMD_SET_NEXTREGS}
    check((nxt <= have) == (model == 4), "the Next's commands advertised on the wrong machine")
    check(CMD_READ_MEM not in have, "DZRP 2.0's READ_MEM advertised")

    regs = d.regs()
    check(len(regs["slots"]) == nslots, f"{len(regs['slots'])} slots, expected {nslots}")

    # the program, interrupts off so nothing but it runs
    d.cmd(CMD_INTERRUPT_ON_OFF, b"\0")
    d.write_mem(0x8000, PROG)
    d.write_mem(0x9000, b"\0")
    check(d.read_mem(0x8000, len(PROG)) == PROG, "memory read back differs")
    check(d.cmd(CMD_READ_MEM, struct.pack("<BHH", 0, 0x8000, 4)) == PROG[:4], "READ_MEM (2.0) differs")
    regs = d.regs()
    bank, ofs = bank_of(model, regs["slots"], 0x8000)
    got = d.cmd(CMD_READ_BANK_MEM, struct.pack("<BHH", bank, ofs, len(PROG)))
    check(got == PROG, f"bank {bank} at {ofs:04X}: {got.hex()}")
    d.cmd(CMD_WRITE_BANK_MEM, struct.pack("<BH", bank, ofs + 0x0A) + b"\x77")
    check(d.read_mem(0x800A, 1) == b"\x77", "WRITE_BANK_MEM did not reach the cpu's view")
    d.write_mem(0x800A, b"\0")
    d.set_reg(REG_SP, 0x9F00)
    d.set_reg(REG_PC, 0x8000)
    check(d.regs()["PC"] == 0x8000, "PC not set")

    # a breakpoint after the CALL
    r = d.cmd(CMD_ADD_BREAKPOINT, struct.pack("<HB", 0x8005, 0) + b"\0")
    bp = struct.unpack("<H", r)[0]
    check(bp != 0, "no breakpoint id")
    d.cont()
    why, adr, _, _ = d.pause_ntf()
    check((why, adr) == (2, 0x8005), f"breakpoint: reason {why} at {adr:04X}")
    regs = d.regs()
    check(regs["PC"] == 0x8005 and regs["AF"] >> 8 == 2, f"at the breakpoint: PC {regs['PC']:04X} A {regs['AF'] >> 8}")

    # step over the CALL: one temp point after it (the user's there too: the step wins)
    d.set_reg(REG_PC, 0x8002)
    d.set_reg(REG_A, 1)
    d.cont(0x8005)
    why, adr, _, _ = d.pause_ntf()
    regs = d.regs()
    check(why == 0 and regs["PC"] == 0x8005 and regs["AF"] >> 8 == 2,
          f"step over: reason {why}, PC {regs['PC']:04X}, A {regs['AF'] >> 8}")
    d.cmd(CMD_REMOVE_BREAKPOINT, struct.pack("<H", bp))

    # step into it: the call's target and the next instruction
    d.set_reg(REG_PC, 0x8002)
    d.set_reg(REG_A, 1)
    d.cont(0x8005, 0x8010)
    why, _, _, _ = d.pause_ntf()
    regs = d.regs()
    check(why == 0 and regs["PC"] == 0x8010, f"step into: reason {why}, PC {regs['PC']:04X}")
    check(d.read_mem(regs["SP"], 2) == b"\x05\x80", "the return address is not on the stack")

    # a write watchpoint on #9000: stops after the LD that writes it
    d.cmd(CMD_ADD_WATCHPOINT, struct.pack("<HBHB", 0x9000, 0, 1, 2))
    d.cont()
    why, adr, _, _ = d.pause_ntf()
    regs = d.regs()
    check((why, adr) == (4, 0x9000) and regs["PC"] == 0x8008,
          f"watchpoint: reason {why} at {adr:04X}, PC {regs['PC']:04X}")
    check(d.read_mem(0x9000, 1) == b"\x02", "the watched byte")
    d.cmd(CMD_REMOVE_WATCHPOINT, struct.pack("<HBHB", 0x9000, 0, 1, 2))

    # run, then pause: the reply first, then the notification
    d.cont()
    time.sleep(0.2)
    d.cmd(CMD_PAUSE)
    why, adr, _, _ = d.pause_ntf()
    check(why == 1 and adr == 0x8008, f"pause: reason {why} at {adr:04X}")

    # a breakpoint bound to the bank #8008 is in
    regs = d.regs()
    bank, _ = bank_of(model, regs["slots"], 0x8008)
    r = d.cmd(CMD_ADD_BREAKPOINT, struct.pack("<HB", 0x8008, bank + 1) + b"\0")
    bp = struct.unpack("<H", r)[0]
    d.cont()
    why, adr, bank1, _ = d.pause_ntf()
    check((why, adr) == (2, 0x8008), f"bank breakpoint: reason {why} at {adr:04X}")
    check(bank1 == bank + 1, f"bank in the notification {bank1 - 1}, expected {bank}")
    d.cmd(CMD_REMOVE_BREAKPOINT, struct.pack("<H", bp))

    # a state saved, the memory changed, the state put back
    state = d.cmd(CMD_READ_STATE)
    check(len(state) > 0x4000, f"state of {len(state)} bytes")
    d.write_mem(0x9000, b"\x55")
    d.cmd(CMD_WRITE_STATE, state)
    check(d.read_mem(0x9000, 1) == b"\x02", "the state put back did not restore memory")

    if model == 4:
        check(len(d.cmd(CMD_GET_NEXTREG, b"\x00")) == 1, "GET_NEXTREG")
        check(len(d.cmd(CMD_GET_SPRITES_PALETTE, b"\0")) == 512, "sprite palette")
        check(len(d.cmd(CMD_GET_SPRITES_CLIP)) == 5, "sprite clip window")
        check(len(d.cmd(CMD_GET_SPRITES, b"\x00\x02")) == 10, "sprite attributes")
        check(len(d.cmd(CMD_GET_SPRITE_PATTERNS, struct.pack("<HH", 0, 1))) == 256, "sprite pattern")
        d.cmd(CMD_SET_NEXTREGS, b"\x14\xE3")		# the global transparency colour
        check(d.cmd(CMD_GET_NEXTREG, b"\x14") == b"\xE3", "SET_NEXTREGS")

    # the user's own breakpoint (--bp #8021): DeZog's stop too, its reason in words
    d.write_mem(0x8020, b"\x00\x18\xFE")
    d.set_reg(REG_PC, 0x8020)
    d.cont()
    why, adr, _, text = d.pause_ntf()
    check((why, adr) == (255, 0x8021) and text == "Xpeccy+ breakpoint: fetch #8021",
          f"own breakpoint: reason {why} at {adr:04X}, '{text}'")

    # a command not served: an empty reply, the session goes on
    check(d.cmd(99) == b"", "a command not served")
    d.cmd(CMD_CLOSE)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("machines", nargs="*", default=list(MACHINES))
    ap.add_argument("--app", help="the staged build folder (default: the newest dev build)")
    ap.add_argument("--port", type=int, default=14321)
    a = ap.parse_args()
    if a.app:
        app = Path(a.app)
    else:
        cands = [d for d in (ROOT / "build" / "dist").glob("*-dev+*") if (d / EXE).exists()]
        if not cands:
            sys.exit("no dev build in build/dist; pass --app")
        app = max(cands, key=lambda d: (d / EXE).stat().st_mtime)
    bad = 0
    for machine in a.machines:
        if machine not in MACHINES:
            sys.exit(f"unknown machine {machine}: {' '.join(MACHINES)}")
        with tempfile.TemporaryDirectory(prefix="xdzrp-") as tmp:
            cfg = Path(tmp) / "config"
            shutil.copytree(app / "config", cfg)
            cmd = [str(app / EXE), "--confdir", str(cfg), "-m", machine,
                   "--bench", "100000000", "--dezog-port", str(a.port), "--bp", "0x8021"]
            proc = subprocess.Popen(cmd, cwd=app, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            d = None
            try:
                d = Dzrp(a.port)
                run_machine(d, machine)
                print(f"{machine}: ok")
            except Fail as e:
                print(f"{machine}: FAIL {e}")
                bad += 1
            finally:
                if d and d.sock:
                    d.sock.close()
                proc.kill()
                proc.wait()
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
