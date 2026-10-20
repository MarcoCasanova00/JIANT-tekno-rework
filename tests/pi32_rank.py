#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
"""(JIANT 0.9.19) The audio path's functions ranked by estimated pi32v2 cycles: callgrind's exact counts on the host
(tests/cpu_vg.c's sections, the instructions of each function itself) x the cycles per instruction of the target's
code for it (tests/pi32_cycles.py: the timing model of Felucca discussion #283, zemuro / Ruslan Mazavin's
measurements of the pi32v2). A host function stands for the target's function of that name and those of its target
callees the host compiler inlined into it (no host symbol of their own); their cycles per instruction are their loops'
(inner loops x4), or the straight code's when there are none. Shown per audio half (128 samples, 2.9 ms), with the
group's costliest loop and its hazards.
An estimate for ranking (it takes a host instruction for a target one), not the device's TIMER4 load meter.
  python3 tests/pi32_rank.py [-n N] [SECTION...]   (default: song/cputest mix/v12.c0 mix/v12; needs build/felucca.dis,
                                                    valgrind; builds build/host/pi32_rank_vg)"""
import glob
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pi32_cycles as pc                                       # noqa: E402

SAMPLES = 256 * 32          # cpu_vg.c: NB blocks of CTL
HALF = 128
ALIAS = {"mix_part": "mix_part_m0", "fx_buses": "fx_run0"}   # host function -> the target's (always_inline there)
CALL = re.compile(r"call -?\d+ <([A-Za-z_][\w.]*) :")


def main():
    a = sys.argv[1:]
    top = 30
    if a[:1] == ["-n"]:
        top, a = int(a[1]), a[2:]
    secs = a or ["song/cputest", "mix/v12.c0", "mix/v12"]
    binary = "build/host/pi32_rank_vg"           # (its own: not run_tests.sh's cpu_vg)
    subprocess.run(f"cc -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o {binary} tests/cpu_vg.c -lm", shell=True, check=True)
    host = {ln.split()[-1].split(".")[0] for ln in
            subprocess.run(["nm", binary], capture_output=True, text=True).stdout.splitlines() if ln.strip()}
    tmp = os.environ.get("PI32_RANK_CG") or tempfile.mkdtemp(prefix="pi32_rank_")   # (set: kept, and reused)
    prefix = os.path.join(tmp, "cg.out")
    if not glob.glob(prefix + "*"):
        subprocess.run(["valgrind", "--tool=callgrind", "--collect-atstart=yes", "--instr-atstart=no",
                        f"--callgrind-out-file={prefix}", binary], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dumps = {}
    for f in glob.glob(prefix + "*"):
        m = re.search(r"^desc: Trigger: Client Request: (\S+)", open(f, errors="replace").read(), re.M)
        if m:
            dumps[m.group(1)] = f
    fns = pc.parse("build/felucca.dis")
    calls = {n: {m.group(1) for i in ins for m in [CALL.search(i.text)] if m} for n, ins in fns.items()}
    memo = {}

    def info(n):
        if n not in memo:
            memo[n] = pc.function(fns[n])
        return memo[n]

    def group(n):
        g, todo = {n}, [n]
        while todo:
            for c in calls.get(todo.pop(), ()):
                if c not in g and c not in host and c in fns:
                    g.add(c)
                    todo.append(c)
        return g

    for s in secs:
        if s not in dumps:
            print(f"{s}: no such section")
            continue
        ann = subprocess.run(["callgrind_annotate", "--inclusive=no", "--threshold=100", dumps[s]],
                             capture_output=True, text=True).stdout
        ir = {}
        for line in ann.splitlines():
            m = re.match(r"^\s*([\d,]+)\s+\([^)]*\)\s+\S*?:([A-Za-z_]\w*)", line)
            if m:
                k = m.group(2)
                ir[k] = ir.get(k, 0) + int(m.group(1).replace(",", ""))
        total = sum(ir.values())
        rows = []
        for n, v in ir.items():
            h = v / SAMPLES * HALF
            n = ALIAS.get(n, n)
            if n not in fns:
                rows.append((h * 1.8, n, h, None, None, []))      # (no target code of that name: an average CPI)
                continue
            g = group(n)
            cyc = sum(info(x)["cyc"] for x in g)
            ins = sum(info(x)["ins"] for x in g)
            if ins:
                cpi = cyc / ins
            else:
                c, i = info(n)["linear"]
                cpi = c / max(i, 1)
            worst = max(((r["cyc"], x, r) for x in g for r in info(x)["loops"]), default=None, key=lambda t: t[0])
            rows.append((h * cpi, n, h, cpi, worst, sorted(g - {n})))
        rows.sort(key=lambda r: -r[0])
        est = sum(r[0] for r in rows)
        print(f"== {s}: host {total / SAMPLES * HALF:,.0f} instructions an audio half; estimated pi32v2 "
              f"{est:,.0f} cycles ({est / (2.9e-3 * 320e6) * 100:.0f} % of a 320 MHz core's 2.9 ms)")
        for c, n, h, cpi, worst, g in rows[:top]:
            w = ""
            if worst:
                r = worst[2]
                w = f"  [{worst[1]} {r['head']:x}: {r['cyc_iter']:.0f} cyc/iter, {r['insns']} insns; " + \
                    ", ".join(pc.hazards(r)) + "]"
            inl = f" (+{','.join(g)})" if g else ""
            print(f"  {c:9,.0f} cyc  {n}{inl}: host {h:8,.0f} x {cpi if cpi else 1.8:.2f}{w}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
