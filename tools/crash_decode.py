#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
"""(JIANT 0.9.12) Decode a crash screen (a photo's numbers), MENU > ABOUT's LAST CRASH lines or a settings backup
against the build's ELF: which function the PC and RETS are in, what the EMU / DBG bits mean, what the short code
says, where the second core was, which bus master made a bad access.

The crash screen (firmware/src/main.c fm1_fault), top to bottom:
  0.9.19 on   as 0.9.12, then "SP .. C2 .." (the stack pointer; the second core's PC when it ran); 10 s on the screen
  0.9.12 on   JIANT CRASH / "v0.9.12  CORE 0  2 CORES" / "CODE IL-01" / VEC .. / PC .. / EMU .. / DBG .. / RETS ..
  0.7.0-0.9.11  JIANT CRASH,   then five unlabelled 8-digit hex numbers: VEC PC EMU DBG RETS
  up to 0.6.x   FELUCCA CRASH, the same five numbers in the same order
MENU > ABOUT > LAST CRASH (0.9.12 on): one line "PC01 2C 0.9.17 02820A80 02034402" (code and vector, cores, version,
PC, RETS); (0.9.19) under it "CRASHES n  CORE c  UP t S", "SP ..  AT AUDIO" / "SP ..  UI stage.page",
"EMU .. DBG ..", "C2 PC ..", "BUS rd wr lim", "CLK BOOT b PLL m", and THE CRASH BEFORE's line.

  tools/crash_decode.py VEC PC EMU DBG RETS        the five numbers, in the screen's order (hex, 0x optional)
  tools/crash_decode.py --pc PC [--rets RETS]      just the addresses
  tools/crash_decode.py --line "PC01 2C 0.9.17 02820A80 02034402" [--about "CRASHES 1  CORE 0  UP 754 S" ...]
                                                   ABOUT's line, and the lines under it as photographed
  tools/crash_decode.py --backup backup.json       a settings backup of the web editor / installer (its settings
                                                   object holds every row: the line, the detail, the crash before)
  options: --elf build/felucca.elf (default; it must be the build of the version that crashed: check out that
           commit, `git log --grep '^0.6.80'`, and ./build.sh), --dis build/felucca.dis (the listing: shows the
           instructions around the PC when it is there); --builds DIR picks DIR/v<version>-*/felucca.elf by the
           version the line names (a folder per release build, as the maintainer keeps them)"""
import argparse
import base64
import glob
import json
import os
import re
import struct
import sys

# DBG_MSG (0x1EEE244) bits, from the JieLi AC79 SDK's cpu/wl82/debug.c debug_msg[]
DBG_BITS = {0: "wdt_timeout", 1: "prp_rd_mmu", 2: "prp_wr_limit", 3: "prp_wr_mmu", 4: "axi_rd_inv", 5: "axi_wr_inv",
            6: "c1_rd_mmu", 7: "c0_rd_mmu", 8: "c1_wr_mmu", 9: "c0_wr_mmu", 10: "c1_pc_limit", 11: "c1_wr_limit",
            12: "c0_pc_limit", 13: "c0_wr_limit", 14: "dmc_pnd", 15: "dbg_pnd", 16: "c1_fetch_bus_inv",
            17: "c1_rd_bus_inv", 18: "c1_wr_bus_inv", 19: "c0_fetch_bus_inv", 20: "c0_rd_bus_inv", 21: "c0_wr_bus_inv"}
# EMU_MSG (0x1EEF0D4, the faulting core's bank) bits, the SDK's emu_msg[]
EMU_BITS = {0: "misaligned access", 1: "illegal instruction", 2: "divide by zero", 3: "stack outside its limits",
            8: "ETM watchpoint", 16: "fpu invalid", 17: "fpu inf", 18: "fpu tiny", 19: "fpu huge", 20: "fpu inexact"}
# the short code (firmware/src/cpu_safe.c crash_code): the first cause found, EMU first
CODES = {"MA": "misaligned access (EMU 0)", "IL": "illegal instruction (EMU 1): a jump into data, a corrupted "
         "return address or a misread fetch", "DZ": "divide by zero (EMU 2)", "ST": "stack overflow (EMU 3)",
         "PC": "fetch outside the code (DBG 10 / 12: a wild jump, a corrupted return address)", "WR": "write into a "
         "protected area (DBG 2, 11, 13: a stack guard band or the vectors)", "MM": "MMU (DBG 1, 3, 6..9)",
         "BU": "bus: an access to unmapped memory (DBG 4, 5, 16..21: a NULL or wild pointer, a write into the flash "
         "window)", "WD": "a hang: the watchdog (a reset with no exception, or (0.9.19) DBG 0 raised as one)",
         "??": "no cause bit set (another vector)"}
VEC = {1: "the CPU exception (every guard ends here)"}
# the bus masters the debug unit names (0x1EEE36C..37C; the SDK's debug.c prints them raw): 0x100 is CPU 0 as the
# SDK reads it; the others are not documented (DMA engines, CPU 1)
MASTERS = {0x100: "CPU 0"}
# CLK BOOT (main.c cpu_clock: hal/fm1_clk320.h's check as the boot stage left the clock)
CLK = {0: "the V15 boot stage's 360 MHz (JIANT then switches to 320 if set)", 1: "already 320 MHz (another boot "
       "stage, M-VAVE's open one, or a soft reset that kept JIANT's switch)", 2: "SYS not on PLL2's VCO x 2/3",
       3: "a PLL multiplier that is neither 360's nor 320's", 4: "another reference clock", 5: "other HSB / LSB "
       "dividers", 6: "SYS not on the PLL", 7: "USB on PLL2", 8: "SDRAM on", 15: "not known (an older record)"}


def elf_syms(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1:
        sys.exit(f"{path}: not a 32-bit ELF")
    e = "<" if d[5] == 1 else ">"
    shoff, = struct.unpack_from(e + "I", d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(e + "HHH", d, 0x2E)
    secs = [struct.unpack_from(e + "IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    names = secs[shstrndx]

    def sname(off):
        s = d[names[4] + off:d.index(b"\0", names[4] + off)]
        return s.decode()
    sections = [(sname(s[0]), s[3], s[5]) for s in secs]          # name, addr, size
    syms = []
    for s in secs:
        if s[1] != 2:                                             # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for k in range(s[5] // 16):
            nm, val, size, info, _, shndx = struct.unpack_from(e + "IIIBBH", d, s[4] + k * 16)
            if (info & 15) not in (0, 2) or not nm or shndx == 0:   # NOTYPE / FUNC, defined
                continue
            name = d[strtab[4] + nm:d.index(b"\0", strtab[4] + nm)].decode(errors="replace")
            if name.startswith(("$", ".L")):
                continue
            syms.append((val, size, name, info & 15))
    syms.sort()
    return syms, sections


def where(addr, syms, sections):
    sec = next((n for n, a, sz in sections if a and a <= addr < a + sz), None)
    best = None
    for val, size, name, typ in syms:
        if val > addr:
            break
        if typ == 2 and (size == 0 or addr < val + size):
            best = (val, name)
        elif typ == 2 and best is None:
            best = (val, name)
    if not best or not sec:
        return f"0x{addr:08X}: no function ({sec or 'outside every section'})"
    return f"0x{addr:08X}: {best[1]} + 0x{addr - best[0]:X}  [{sec}]"


def region(addr):
    if 0x02000000 <= addr < 0x02100000:
        return "XIP flash (code and constants)"
    if 0x01C00000 <= addr < 0x01C80000:
        return "RAM" + (" (the RAM code: .ram_text / .c1_text / .hot_text)" if addr >= 0x01C6D000 else "")
    return "not code nor RAM: a wild value"


def bits(v, table):
    return [f"{b} {table.get(b, 'reserved')}" for b in range(32) if (v >> b) & 1]


def code_of(emu, dbg):
    for b, c in ((0, "MA"), (1, "IL"), (2, "DZ"), (3, "ST")):
        if (emu >> b) & 1:
            return c
    if dbg & 1:
        return "WD"
    for mask, c in (((1 << 10) | (1 << 12), "PC"), ((1 << 2) | (1 << 11) | (1 << 13), "WR"),
                    ((1 << 1) | (1 << 3) | (15 << 6), "MM"), ((3 << 4) | (63 << 16), "BU")):
        if dbg & mask:
            return c
    return "??"


def listing(dis, addr, n=6):
    if not dis or not os.path.exists(dis):
        return []
    want = f"{addr:x}:"
    lines = open(dis, errors="replace").read().splitlines()
    for i, ln in enumerate(lines):
        if ln.strip().startswith(want):
            return lines[max(0, i - n):i + n]
    return []


def parse_line(s):
    """'PC01 2C 0.9.17 02820A80 02034402' -> dict, or None"""
    m = re.match(r"^\s*([A-Z?]{2})([0-9A-F]{2})\s+([12])C\s+(\S+)\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s*$", s.upper())
    if not m:
        return None
    code, vec, cores, ver, pc, rets = m.groups()
    return {"code": code, "vec": int(vec, 16), "cores": int(cores), "version": ver.lower(), "pc": int(pc, 16),
            "rets": int(rets, 16)}


def parse_about(lines):
    """ABOUT's lines under LAST CRASH (0.9.19), as photographed, into the detail's fields"""
    d = {}
    for s in lines:
        s = " ".join(s.upper().split())
        m = re.match(r"CRASHES (\d+) CORE (\S) UP (\d+) S", s)
        if m:
            d["count"], d["core"], d["up_s"] = int(m.group(1)), m.group(2), int(m.group(3))
            continue
        m = re.match(r"SP ([0-9A-F]{8}) (AT AUDIO|UI (\d+)\.(\d+))", s)
        if m:
            d["sp"] = int(m.group(1), 16)
            d["audio"] = m.group(2) == "AT AUDIO"
            if m.group(3):
                d["stage"], d["page"] = int(m.group(3)), int(m.group(4))
            continue
        m = re.match(r"EMU ([0-9A-F]{8}) DBG ([0-9A-F]{8})", s)
        if m:
            d["emu"], d["dbg"] = int(m.group(1), 16), int(m.group(2), 16)
            continue
        m = re.match(r"C2 PC ([0-9A-F]{8})", s)
        if m:
            d["other_pc"] = int(m.group(1), 16)
            continue
        m = re.match(r"BUS ([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{8})", s)
        if m:
            d["bus"] = tuple(int(x, 16) for x in m.groups())
            continue
        m = re.match(r"CLK BOOT (\d+) PLL (\d+)", s)
        if m:
            d["clk"], d["mult"] = int(m.group(1)), int(m.group(2))
            continue
        print(f"(not read: {s!r})")
    return d


FAV_OFF = 52            # persist_t: magic, palette, lowcut, zoom (16) + panel_t (32) + bold (4); then the favourites
ROW = 32


def parse_backup(path):
    """the settings object (id 1) of a felucca-backup .json: the LAST CRASH line, the detail, the crash before"""
    with open(path, "rb") as f:
        doc = json.loads(f.read().decode("utf-8", errors="replace"))
    objs = doc.get("objects") if isinstance(doc, dict) else None
    o = next((x for x in objs or [] if isinstance(x, dict) and x.get("id") == 1), None)
    if not o or not isinstance(o.get("data"), str):
        sys.exit(f"{path}: no settings object (id 1) in it")
    b = base64.b64decode(o["data"], validate=False)
    if len(b) < FAV_OFF + 16 * ROW:
        sys.exit(f"{path}: the settings object is {len(b)} bytes: too short for the favourites rows")
    row = lambda k: b[FAV_OFF + k * ROW:FAV_OFF + (k + 1) * ROW]
    text = lambda r: r.decode("ascii", errors="replace") if all(32 <= c <= 90 for c in r) else None
    out = {"line": text(row(13)), "prev": text(row(9)), "about": []}
    det, det2 = row(8), row(6)
    u32 = lambda r, i: struct.unpack_from("<I", r, i)[0]
    if det[:2] == b"JD":
        d = {"count": det[2] | det[3] << 8, "core": "-" if det[4] > 1 else str(det[4]), "flags": det[5],
             "stage": det[6], "page": det[7], "sp": u32(det, 8), "emu": u32(det, 12), "dbg": u32(det, 16),
             "up_s": u32(det, 20) // 1000, "etm": (u32(det, 24), u32(det, 28)), "audio": bool(det[5] & 16)}
        if det2[:2] == b"JE":
            d["clk"], d["mult"] = det2[2], det2[3] + 200
            if det[5] & 32:
                d["other_pc"] = u32(det2, 4)
            ids = [u32(det2, 8 + 4 * i) for i in range(5)]       # PRP_WR_LIMIT_ID, AXI_RD, AXI_WR, MMU_W, MMU_R
            d["bus"] = (ids[1], ids[2], ids[0])
            d["limits"] = (det2[28], det2[29], det2[30])
        out["detail"] = d
    return out


def report_detail(d, syms, sections):
    if "count" in d:
        print(f"CRASHES {d['count']} logged on this unit; this one on core {d['core']} ({'a hang' if d['core'] == '-' else 'the core that faulted'}), "
              f"{d.get('up_s', '?')} s after the start")
    if "sp" in d and d.get("core") != "-":
        sp = d["sp"]
        stack = ("the main loop's user stack" if 0x01C76100 <= sp < 0x01C7A000 else "the supervisor stack" if
                 0x01C7A100 <= sp < 0x01C7C000 else "the second core's stack" if 0x01C20000 <= sp < 0x01C6FC00 else
                 "outside every stack (a corrupted SP)")
        print(f"SP   0x{sp:08X}: {stack}; " + ("the audio interrupt was running" if d.get("audio") else
              f"the main loop at stage {d.get('stage')} (1 input, 2 drawing, 9 waiting) on page {d.get('page')}"))
    if "emu" in d:
        print(f"EMU  0x{d['emu']:08X}: " + (", ".join(bits(d["emu"], EMU_BITS)) or "none"))
        print(f"DBG  0x{d['dbg']:08X}: " + (", ".join(bits(d["dbg"], DBG_BITS)) or "none"))
    if "other_pc" in d:
        print(f"C2   {where(d['other_pc'], syms, sections)}   (the second core, paused as the fault was taken)")
    if "bus" in d:
        rd, wr, lim = d["bus"]
        print(f"BUS  read 0x{rd:X} ({MASTERS.get(rd, 'not a known master')}), write 0x{wr:X} "
              f"({MASTERS.get(wr, 'not a known master')}), write-limit 0x{lim:X} ({MASTERS.get(lim, 'not a known master')})")
    if "clk" in d:
        mhz = d["mult"] * 4 // 3
        print(f"CLK  boot stage {d['clk']}: {CLK.get(d['clk'], '?')}; PLL multiplier {d['mult']} (~{mhz} MHz as it crashed)")
    if "etm" in d:
        print(f"ETM  {where(d['etm'][0], syms, sections)}; {where(d['etm'][1], syms, sections)}   (the last branches)")


def find_elf(builds, version):
    if not builds or not version:
        return None
    v = version if version.startswith("v") else "v" + version
    hits = sorted(glob.glob(os.path.join(builds, v + "-*", "felucca.elf")))
    return hits[0] if hits else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("nums", nargs="*", help="VEC PC EMU DBG RETS, as on the screen")
    ap.add_argument("--pc")
    ap.add_argument("--rets")
    ap.add_argument("--line", help="MENU > ABOUT's LAST CRASH line")
    ap.add_argument("--about", nargs="*", default=[], help="the lines under it (0.9.19)")
    ap.add_argument("--backup", help="a felucca-backup .json (the web editor's / installer's)")
    ap.add_argument("--elf", default="build/felucca.elf")
    ap.add_argument("--dis", default="build/felucca.dis")
    ap.add_argument("--builds", default=os.environ.get("CRASH_BUILDS"), help="a folder of release builds, v<ver>-<sha>/")
    a = ap.parse_args()
    h = lambda s: int(s, 16)
    vec = emu = dbg = None
    code = version = None
    detail = {}
    pc, rets = (h(a.pc) if a.pc else None), (h(a.rets) if a.rets else None)
    line, prev = a.line, None
    if a.backup:
        bk = parse_backup(a.backup)
        line, prev, detail = bk["line"], bk["prev"], bk.get("detail", {})
        if not line:
            sys.exit("the backup holds no crash record")
        print(f"the backup's LAST CRASH: {line}")
    if line:
        L = parse_line(line)
        if not L:
            sys.exit(f"not a LAST CRASH line: {line!r} (expected like 'PC01 2C 0.9.17 02820A80 02034402')")
        code, vec, pc, rets, version = L["code"], L["vec"], L["pc"], L["rets"], L["version"]
        print(f"LINE code {code}, vector {vec}, {L['cores']} core(s) running, firmware {version}")
    if a.about:
        detail.update(parse_about(a.about))
    if a.nums:
        if len(a.nums) != 5:
            sys.exit("five numbers, in the screen's order: VEC PC EMU DBG RETS")
        vec, pc, emu, dbg, rets = (h(x) for x in a.nums)
    if pc is None:
        ap.error("the five numbers, --pc, --line or --backup")
    elf = a.elf
    found = find_elf(a.builds, version)
    if found:
        elf = found
        a.dis = os.path.join(os.path.dirname(found), "felucca.dis")
        print(f"ELF  {elf} (the build of {version})")
    elif version and a.builds:
        print(f"(no build of {version} under {a.builds}: decoding against {elf}; the functions may be wrong)")
    syms, sections = elf_syms(elf)
    if vec is not None:
        print(f"VEC  0x{vec:X}: " + VEC.get(vec, "an interrupt with no handler (a stray vector)" if vec != 0xFF else
                                            "a hang (the watchdog reset the unit: PC / RETS are the main loop's stage and page)"))
    if vec == 0xFF:
        print(f"     stage {pc} (1 input, 2 drawing, 9 waiting), page {rets}")
    else:
        print(f"PC   {where(pc, syms, sections)}   {region(pc)}")
        if rets is not None:
            print(f"RETS {where(rets, syms, sections)}   (the caller: where the faulting function returns to)")
    if emu is None and "emu" in detail:
        emu, dbg = detail["emu"], detail["dbg"]
    if vec == 0xFF:
        print(f"CODE WD: {CODES['WD']}")
        detail.pop("emu", None)
        detail.pop("dbg", None)
    elif emu is not None:
        print(f"EMU  0x{emu:08X}: " + (", ".join(bits(emu, EMU_BITS)) or "none"))
        print(f"DBG  0x{dbg:08X}: " + (", ".join(bits(dbg, DBG_BITS)) or "none"))
        c = code_of(emu, dbg)
        print(f"CODE {c}: {CODES[c]}")
        if dbg & ((1 << 6) | (1 << 8) | (1 << 10) | (1 << 11) | (7 << 16)):
            print("     a core-1 bit is set: the second core misbehaved too (MENU > SYSTEM > CPU 1 CORE)")
        detail.pop("emu", None)
        detail.pop("dbg", None)
    elif code:
        print(f"CODE {code}: {CODES.get(code, '?')}")
    report_detail(detail, syms, sections)
    if prev:
        print(f"THE CRASH BEFORE: {prev}")
        P = parse_line(prev)
        if P and P["vec"] != 0xFF:
            pe = find_elf(a.builds, P["version"])
            if pe and pe != elf:
                syms, sections = elf_syms(pe)
                print(f"     (against {pe})")
            elif P["version"] != version:
                print(f"     (another version, {P['version']}: the functions below may be wrong)")
            print(f"     PC   {where(P['pc'], syms, sections)}")
            print(f"     RETS {where(P['rets'], syms, sections)}")
    if vec != 0xFF:
        for ln in listing(a.dis, pc):
            print("   " + ln)


if __name__ == "__main__":
    main()
