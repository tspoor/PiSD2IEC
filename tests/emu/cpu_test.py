#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 T. Spoor
#
# cpu_test.py - tests for the 6502 core, on the PC
# Part of PiSD2IEC, a minimal sd2iec solution for the Pi.

"""
Tests for the 6502 core (src/emu/cpu6502_impl.h), on the PC.

  cpu_test.py lib.so dormann 6502_functional_test.bin
      Klaus Dormann's functional test: must end in the success trap $3469.

  cpu_test.py lib.so harte DIR [opcode ...]
      Tom Harte's SingleStepTests (65x02, 6502/v1 JSON): registers, memory
      and every single bus cycle (address, value, read/write) of 10,000
      cases per opcode. JAM opcodes are skipped.

lib.so is tests/emu/cpu_testlib.c compiled as a shared library (see the
Makefile target 'test-cpu'). The test data is not part of PiSD2IEC.
"""
import ctypes
import json
import os
import sys

JAMS = {0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x92, 0xB2, 0xD2, 0xF2}


def load(lib):
    L = ctypes.CDLL(os.path.abspath(lib))
    L.t_run_until_trap.restype = ctypes.c_uint16
    L.t_get_mem.restype = ctypes.c_uint8
    return L


def dormann(L, path):
    data = open(path, "rb").read()
    L.t_clear()
    L.t_load((ctypes.c_uint8 * len(data)).from_buffer_copy(data), len(data), 0)
    L.t_set_regs(0x0400, 0, 0, 0, 0xFF, 0x24)
    cycles = ctypes.c_uint64()
    pc = L.t_run_until_trap(ctypes.c_uint64(200_000_000), ctypes.byref(cycles))
    ok = pc == 0x3469
    print("Dormann: trap at $%04X after %d cycles: %s" % (pc, cycles.value, "PASS" if ok else "FAIL"))
    return ok


def harte(L, folder, only):
    pc, a, x, y, s, p = (ctypes.c_uint16(), ctypes.c_uint8(), ctypes.c_uint8(),
                         ctypes.c_uint8(), ctypes.c_uint8(), ctypes.c_uint8())
    la, lv, lw = ctypes.c_uint16(), ctypes.c_uint8(), ctypes.c_uint8()
    total_fail = 0
    for op in range(256):
        if op in JAMS or (only and op not in only):
            continue
        cases = json.load(open(os.path.join(folder, "%02x.json" % op)))
        fails = 0
        for t in cases:
            i, f = t["initial"], t["final"]
            L.t_clear()
            for addr, val in i["ram"]:
                L.t_set_mem(addr, val)
            L.t_set_regs(i["pc"], i["a"], i["x"], i["y"], i["s"], i["p"])
            n = L.t_step()
            L.t_get_regs(ctypes.byref(pc), ctypes.byref(a), ctypes.byref(x),
                         ctypes.byref(y), ctypes.byref(s), ctypes.byref(p))
            got = {"pc": pc.value, "a": a.value, "x": x.value, "y": y.value,
                   "s": s.value, "p": p.value}
            err = [k for k in got if got[k] != f[k]]
            err += ["mem $%04X" % addr for addr, val in f["ram"] if L.t_get_mem(addr) != val]
            if n != len(t["cycles"]):
                err.append("%d cycles instead of %d" % (n, len(t["cycles"])))
            else:
                for k, (addr, val, kind) in enumerate(t["cycles"]):
                    L.t_log(k, ctypes.byref(la), ctypes.byref(lv), ctypes.byref(lw))
                    if (la.value, lv.value, lw.value) != (addr, val, kind == "write"):
                        err.append("cycle %d: %04X %02X %s, expected %04X %02X %s" % (
                            k + 1, la.value, lv.value, "w" if lw.value else "r",
                            addr, val, kind[0]))
                        break
            if err:
                if fails < 3:
                    print("  %02X %s: %s" % (op, t["name"], "; ".join(err[:4])))
                fails += 1
        if fails:
            print("opcode %02X: %d of %d failed" % (op, fails, len(cases)))
        total_fail += fails
    print("Harte: %s" % ("PASS" if total_fail == 0 else "%d failures" % total_fail))
    return total_fail == 0


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    L = load(sys.argv[1])
    if sys.argv[2] == "dormann":
        ok = dormann(L, sys.argv[3])
    else:
        ok = harte(L, sys.argv[3], {int(o, 16) for o in sys.argv[4:]})
    sys.exit(0 if ok else 1)
