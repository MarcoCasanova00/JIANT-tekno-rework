#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
"""(JIANT 0.9.19) The worst stack depth of the build, from its disassembly (build/felucca.dis): each function's frame
(its pushes and `sp += -N`), the call graph (direct calls, tail gotos into another function; an indirect call
`call rN` may reach any function whose address the image holds: the engines' tables, the jobs), the deepest path from
each root. Recursion is reported (a cycle: its depth unbounded) and cut.

  tools/stack_depth.py [build/felucca.dis] [--elf build/felucca.elf] [--path] [--check]

The roots and the stacks they run on (app.ld, crt0.S):
  main loop    fm1_cstart (crt0: sp = _ustack_top)           the user stack, 15.75 KiB
  audio ISR    isr_alnk0 (+ TIMER5 nested in it)               the stack the core takes an interrupt on
  TIMER5 ISR   isr_timer5
  core 1       fm1_c1_main (its job: mc_job, uac_rs_job)       _c1_stack (6 KiB, its limit from +256)
  the fault    fm1_fatal_common (on top of whatever faulted)
--check fails when a root's depth leaves less than MARGIN of its stack (both ISRs counted on one stack, nested, on top
of the main loop's deepest: the pi32v2 may take the interrupt on the current sp)."""
import argparse
import re
import struct
import sys

FN = re.compile(r"^([A-Za-z_$][\w.$]*):$")
INS = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(.*)$")
PUSH = re.compile(r"\[--sp\] = \{([^}]*)\}")
POP = re.compile(r"\{([^}]*)\} = \[sp\+\+\]")
ADJ = re.compile(r"\bsp \+= (-?\d+)")
CALL = re.compile(r"\bcall\b[^<]*<([\w.$]+)(?:\+0x[0-9A-F]+)?\s*:\s*([0-9a-f]+)\s*>")
GOTO = re.compile(r"\bgoto\b[^<]*<([\w.$]+)(?:\+0x([0-9A-F]+))?\s*:\s*([0-9a-f]+)\s*>")
ICALL = re.compile(r"\bcall r\d+")

STACKS = {"user": 0x01C7A000 - 0x01C76100, "super": 0x01C7C000 - 0x01C7A100, "c1": 6144 - 256}
MARGIN = 1024


def nregs(s):
    n = 0
    for part in s.split(","):
        part = part.strip()
        m = re.match(r"r(\d+)-r(\d+)$", part)
        if m:
            n += abs(int(m.group(1)) - int(m.group(2))) + 1
        elif part:
            n += 1
    return 4 * n


def parse(dis):
    fns, cur = {}, None
    addr = {}
    for ln in open(dis, errors="replace"):
        ln = ln.rstrip("\n")
        m = FN.match(ln)
        if m and not ln.startswith("."):
            cur = m.group(1)
            fns[cur] = {"frame": 0, "calls": set(), "icall": False, "depth": 0, "start": None}
            continue
        m = INS.match(ln)
        if not m or cur is None:
            continue
        f, a, txt = fns[cur], int(m.group(1), 16), m.group(2)
        if f["start"] is None:
            f["start"] = a
            addr[a] = cur
        mp, mo, ma = PUSH.search(txt), POP.search(txt), ADJ.search(txt)
        if mp:
            f["depth"] += nregs(mp.group(1))
        elif mo:
            f["depth"] -= nregs(mo.group(1))
        elif ma:
            f["depth"] -= int(ma.group(1))
        f["frame"] = max(f["frame"], f["depth"])
        mc = CALL.search(txt)
        if mc:
            f["calls"].add(mc.group(1))
        elif ICALL.search(txt):
            f["icall"] = True
        else:
            mg = GOTO.search(txt)
            if mg and mg.group(1) != cur and not mg.group(1).startswith(".") and not mg.group(2):
                f["calls"].add(mg.group(1))          # a tail call: its frame on top of what is left (0 after the pop)
    return fns, addr


def taken(elf, addr):
    """the functions whose address a data word of the image holds (the targets an indirect call may reach)"""
    d = open(elf, "rb").read()
    e = "<" if d[5] == 1 else ">"
    shoff, = struct.unpack_from(e + "I", d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(e + "HHH", d, 0x2E)
    secs = [struct.unpack_from(e + "IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    out = set()
    for s in secs:
        if s[1] != 1:                                  # SHT_PROGBITS
            continue
        blob = d[s[4]:s[4] + s[5]]
        for k in range(0, len(blob) - 3, 2):           # (code holds them as 32-bit immediates, 2-byte aligned)
            v, = struct.unpack_from(e + "I", blob, k)
            if v in addr:
                out.add(addr[v])
    return out


# the indirect calls that are not the engines' and the FX layer's tables: their own targets
SPECIAL = (("fm1_c1_main", ("mc_job", "uac_rs_job")), ("st_", ("fl_",)), ("fl_", ("fl_",)), ("ota_", ("fl_",)),
           ("cpu_clock", ("fm1_clk320_switch",)))
NOT_GENERAL = ("isr_", "fm1_", "_start", "fl_", "mc_job", "uac_rs_job", "fvb_read")


def solve(fns, taken_all):
    memo, cycles = {}, set()
    general = {n for n in taken_all if not n.startswith(NOT_GENERAL)}

    def targets_of(name):
        for pre, pick in SPECIAL:
            if name.startswith(pre):
                return {n for n in taken_all if n.startswith(pick)}
        return general

    def depth(name, stack):
        if name in memo:
            return memo[name]
        f = fns.get(name)
        if f is None:
            return 0, [name + "?"]
        if name in stack:
            cycles.add(" -> ".join(stack[stack.index(name):] + [name]))
            return 0, [name + " (recursion)"]
        stack.append(name)
        best, path = 0, []
        callees = set(f["calls"]) | (targets_of(name) if f["icall"] else set())
        for c in callees:
            d, p = depth(c, stack)
            if d > best:
                best, path = d, p
        stack.pop()
        memo[name] = (f["frame"] + best, [f"{name} ({f['frame']})"] + path)
        return memo[name]
    return depth, cycles


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dis", nargs="?", default="build/felucca.dis")
    ap.add_argument("--elf", default=None)
    ap.add_argument("--path", action="store_true", help="print the deepest path of each root")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    fns, addr = parse(a.dis)
    elf = a.elf or re.sub(r"\.dis$", ".elf", a.dis)
    targets = taken(elf, addr)
    depth, cycles = solve(fns, targets)
    roots = [("main loop", "fm1_cstart"), ("audio ISR", "isr_alnk0"), ("TIMER5 ISR", "isr_timer5"),
             ("core 1", "fm1_c1_main"), ("fault", "fm1_fatal_common")]
    res = {}
    for label, r in roots:
        d, p = depth(r, [])
        res[r] = d
        print(f"{label:11} {r:18} {d:6} B")
        if a.path:
            print("            " + " > ".join(p))
    nested = res["isr_alnk0"] + res["isr_timer5"] + res["fm1_fatal_common"]
    print(f"interrupts  audio + TIMER5 nested + a fault   {nested:6} B   (the supervisor stack: {STACKS['super']} B)")
    print(f"one stack   main + the above                  {res['fm1_cstart'] + nested:6} B   (user + guard + "
          f"supervisor: {STACKS['user'] + 256 + STACKS['super']} B)")
    print(f"core 1      + a fault                         {res['fm1_c1_main'] + res['fm1_fatal_common']:6} B   "
          f"(its stack past the limit's 256: {STACKS['c1']} B)")
    print(f"indirect targets: {len(targets)}; recursion: {len(cycles)}")
    for c in sorted(cycles):
        print("   cycle: " + c)
    if a.check:
        bad = []
        if res["fm1_cstart"] > STACKS["user"] - MARGIN:
            bad.append("main loop")
        if nested > STACKS["super"] - MARGIN:
            bad.append("interrupts")
        if res["fm1_c1_main"] + res["fm1_fatal_common"] > STACKS["c1"] - MARGIN:
            bad.append("core 1")
        if bad:
            sys.exit("stack depth: less than %d B to spare: %s" % (MARGIN, ", ".join(bad)))


if __name__ == "__main__":
    main()
