#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
"""(JIANT 0.9.19) A cycle estimate of the pi32v2's loops, from the objdump listing (build/felucca.dis), for
tests/target_budget.py. The timing model is the one measured by zemuro (Ruslan Mazavin) on an AC791N pi32v2,
code in SRAM with I-cache hits, published in Felucca discussion #283 (hugelton/Felucca). Thanks!

  32-bit load        2 cycles on the memory port; an independent ALU op can issue in its shadow
                     (load, ALU, load = 4); the value reaches an ALU op 1 cycle after the 2, an address 3 after
  64-bit load        4 cycles on the port; the low register when a 32-bit load's value would be, the high one
                     1 later (the reading of "low immediate, high +1" that gives dv_metal_run's 35)
  ALU result         the next ALU op at once; as an address it stalls 2 / 1 / 0 at distance 1 / 2 / 3
  multiply / MAC     issue 2, result 1 cycle late; smin umin smax umax abs and the pair shifts issue 1, result 1 late
  divide             10 + 3 per significant byte of the dividend beyond the first (10..19): DIV_CYC assumed
  stores             n in a row cost n + (n - 1) // 2; any other op between two breaks the run
  [rB+imm] += rV     3 cycles (5 right after writing its base: the address rule)
  bundles  h # t     the slower half; a load in the tail 2, a store 1, a multiply in the head 2
  branches           not taken 1, taken 6 while warm, 9 cold: a loop whose iteration takes at most 3 distinct
                     targets stays warm, 4 -> two of them miss each iteration, 5 or more -> all miss
  rep rN { body }    (JIANT's assumption, not measured in #283) a zero-overhead loop: REP_SETUP cycles to start,
                     then the body back to back, no branch; the "if (rN != 0) goto" after it (an interrupted
                     loop resumed, as the vendor's code has it) not taken
  fetch              after a taken branch fetch restarts at the target's 32-bit word: a 32/48-bit target at
                     2 mod 4 costs 1 more; runs of 48-bit instructions ~1.5 cycles each
  Benchmarks (#283): fm6_op_run's first loop (20 instructions) 40 cycles, upstream's dv_metal_run inner loop (26)
  35: tests/pi32_bench.dis, checked by target_budget.py (CHECKS). Static count ~ half the real cycles.
Caveat: FM-1 runs most DSP in place from flash through the I-cache (a miss is not modelled); the device's own
measure is the TIMER4 load meter. This is an estimate of the steady state of each loop's straight path (forward
branches not taken but those around a divide, the back edge taken, a jump table's first case), meant for ranking and
for finding hazards, not a cycle-exact simulator.

  python3 tests/pi32_cycles.py [DIS] FUNC...     the loops of FUNC: cycles an iteration, stalls by cause, hazards
  PI32_TAKE=addr,.. PI32_FALL=addr,..          conditional branches (hex) taken / not taken on the path
"""
import os
import re
import sys

DIV_CYC = 16            # a divide: 10 + 3 x 2 (a dividend of 3 significant bytes, typical of the Q15 / Q16 audio values)
TAKEN, COLD = 6, 9
CALL_RET = TAKEN + TAKEN    # a call in a loop: there and back (the callee's own cycles are not counted here)
REP_SETUP = 2           # a rep loop's start (not measured in #283: an assumption); its iterations cost no branch
NEST = 4                # an inner loop runs NEST times a pass of the outer one (target_budget.py's weight)

LABEL = re.compile(r"^([A-Za-z_.][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"(?:goto|call) -?\d+ <[^>]*: ([0-9a-f]+) >")
REG = re.compile(r"\b(r1[0-5]|r[0-9]|sp|rets)\b")
PAIR = re.compile(r"\br(\d+)_r(\d+)\b")
RANGE = re.compile(r"\br(\d+)-r(\d+)\b")
ANN = re.compile(r"\s*<[^<>]*>\s*$")         # the listing's note after an address or a constant (not a compare's <)


def regs(s):
    """the registers named in s (pairs and ranges expanded)"""
    out = []
    for m in RANGE.finditer(s):
        a, b = sorted((int(m.group(1)), int(m.group(2))))
        out += [f"r{k}" for k in range(a, b + 1)]
    s = RANGE.sub(" ", s)
    for m in PAIR.finditer(s):
        out += [f"r{m.group(1)}", f"r{m.group(2)}"]
    s = PAIR.sub(" ", s)
    return out + REG.findall(s)


def split(t):
    """lhs, operator, rhs of an assignment, the operator found outside the brackets"""
    depth = 0
    for k, ch in enumerate(t):
        depth += ch in "[{(" and 1 or 0
        depth -= ch in "]})" and 1 or 0
        if depth == 0 and t[k:k + 3] == " = ":
            return t[:k], "=", t[k + 3:]
        if depth == 0 and k + 1 < len(t) and t[k + 1] == "=" and ch in "+-|&^" and t[k - 1:k] == " ":
            return t[:k - 1], ch + "=", t[k + 3:]
    return t, "=", ""


class Insn:
    """one instruction: kind, its registers (ALU sources, address sources, destinations), timing"""
    __slots__ = ("addr", "size", "text", "kind", "src", "asrc", "dst", "wb", "target", "cond", "bundle", "nst", "table")

    def __init__(self, addr, size, text):
        self.addr, self.size, self.text = addr, size, text
        self.bundle = None          # the tail, for a bundle head
        self.table = []             # a jump table's targets
        self.target = None
        self.cond = False
        self.wb = []                # address registers written back (++ / --)
        self.nst = 1                # registers stored (push)
        t = ANN.sub("", text).replace("#", "").strip()
        self.src, self.asrc, self.dst = [], [], []
        m = TARGET.search(text)
        if m:
            self.target = int(m.group(1), 16)
        if t.startswith("call"):
            self.kind = "call"
            self.src = regs(t[4:])
        elif re.match(r"ifs? \(", t) and "goto" in t:
            self.kind, self.cond = "br", True
            c = t[t.index("(") + 1:t.rindex(")")]
            self.src = regs(c)
            self.dst = [f"r{x}" for x in re.findall(r"--r(\d+)", c)]
        elif t.startswith("goto"):
            self.kind = "br"
        elif re.match(r"ifs? \(.*\{$", t):      # a predicated block: the compare; its body follows as plain lines
            self.kind = "alu"
            self.src = regs(t)
        elif re.match(r"rep \d+ r\d+ \{$", t):    # a zero-overhead loop: its body's bytes, the count register
            self.kind = "rep"
            self.src = self.dst = regs(t)
            self.table = [addr + size + int(t.split()[1])]     # (the body's end)
        elif t.startswith(("tbb ", "tbh ")):     # a jump table: the path takes its first entry (parse())
            self.kind = "jt"
            self.src = regs(t)
        elif t == "rts" or re.search(r"\bpc\b", t):
            self.kind = "ret"
            self.src = regs(t)
        elif "[" in t and ("=" in t):
            lhs, op, rhs = split(t)
            if "[" in lhs:
                inner = lhs[lhs.index("[") + 1:lhs.rindex("]")]
                self.asrc = regs(inner)
                self.wb = self._wb(inner)
                if op != "=":
                    self.kind = "rmw"
                    self.src = regs(rhs)
                else:
                    self.kind = "st"
                    self.src = regs(rhs)
                    if rhs.startswith("{") and len(self.src) > 1:      # [--sp] = {rets, r14-r4}: a push
                        self.kind, self.nst = "push", len(self.src)
            else:
                inner = rhs[rhs.index("[") + 1:rhs.rindex("]")]
                self.asrc = regs(inner)
                self.wb = self._wb(inner)
                self.dst = regs(lhs)
                w = rhs.lstrip()
                self.kind = "ld64" if (w.startswith("d[") or len(self.dst) == 2) else "ld"
                if len(self.dst) > 2:
                    self.kind = "pop"   # (h[++r4=r8]: address r4 + r8 written back to r4, both address sources)
        else:
            m = re.match(r"^(.*?)\s*(<=|\+=|-=|\*=|/=|\|=|&=|\^=|<<=|>>>=|>>=|=)\s*(.*)$", t)
            if not m:
                self.kind = "alu"
                self.src = regs(t)
            else:
                lhs, op, rhs = m.group(1), m.group(2), m.group(3)
                self.dst = regs(lhs)
                self.src = regs(rhs) + (self.dst if op != "=" else [])
                if " / " in rhs:
                    self.kind = "div"
                elif "*" in rhs or op == "*=":
                    self.kind = "mul"
                    if op == "+=" or op == "-=":
                        self.src += self.dst
                elif re.match(r"(smin|smax|umin|umax|abs)\(", rhs) or (PAIR.search(lhs) and op in ("<<=", ">>=", ">>>=")):
                    self.kind = "short"
                else:
                    self.kind = "alu"

    @staticmethod
    def _wb(inner):
        return [f"r{x}" for x in re.findall(r"(?:\+\+|--)r(\d+)|r(\d+)(?:\+\+|--)", inner) for x in x if x] + \
            (["sp"] if "--sp" in inner or "sp++" in inner else [])

    def mem(self):
        return self.kind in ("ld", "ld64", "st", "rmw", "push", "pop")


def parse(path, names=None):
    """{function: [Insn]} for the named functions (all when names is None); a function runs to the next label
    not starting with '.'; bundle tails are attached to their head (Insn.bundle)"""
    out, cur, head, jt_base = {}, None, None, 0
    with open(path) as f:
        for line in f:
            m = LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = out.setdefault(name, []) if names is None or name in names else None
                    head = None
                continue
            m = INSN.match(line)
            if cur is not None and m and m.group(3).lstrip().startswith("<") and cur and cur[-1].kind == "jt":
                e = re.match(r"<\s*(\d+)", m.group(3).lstrip())     # an entry: halfwords from the table's start
                if e:
                    if not cur[-1].table:
                        jt_base = int(m.group(1), 16)
                    cur[-1].table.append(jt_base + 2 * int(e.group(1)))
                    cur[-1].target = cur[-1].table[0]
            if cur is None or not m or m.group(3).lstrip().startswith("<"):
                continue
            text = m.group(3)
            try:
                i = Insn(int(m.group(1), 16), len(m.group(2).split()), text.strip())
            except (ValueError, IndexError, AttributeError):     # (a form the parser does not know: a plain op)
                i = Insn(int(m.group(1), 16), len(m.group(2).split()), "nop")
            if head is not None:
                head.bundle = i
                head = None
                cur.append(i)       # kept in the list (the static count sees two), skipped by the walk
                i.nst = -1          # marks a tail
                continue
            if re.search(r"#\s*$", ANN.sub("", text)):
                head = i
            cur.append(i)
    return out


def back_edges(insns):
    """the addresses of the backward branches that close a loop: reachable from their target through the code
    between them (a cycle inside the span), and their target dominates them (every way from the function's entry to
    the branch passes through the target) or they dominate it (a loop entered at its test). Not loops: the jumps to
    and from a block laid out of line (a rare case moved before or after the loop, DSP_RARE), a jump back into code
    two paths share. The address spans alone (before 0.9.19) took them all for loops"""
    body = [i for i in insns if i.nst != -1]
    if not body:
        return set()
    at = {i.addr: k for k, i in enumerate(body)}
    succ = []
    for k, i in enumerate(body):
        nxt = [k + 1] if k + 1 < len(body) and not (i.kind == "ret" or i.kind == "jt" or
                                                    (i.kind == "br" and not i.cond)) else []
        tg = i.table if i.kind == "jt" else ([i.target] if i.kind == "br" and i.target is not None else [])
        succ.append(nxt + [at[t] for t in tg if t in at])

    def dominates(d, x):        # every way from the entry to x through d
        if d == 0 or d == x:
            return True
        seen, todo = {0, d}, [0]
        while todo and x not in seen:
            for y in succ[todo.pop()]:
                if y not in seen:
                    seen.add(y)
                    todo.append(y)
        return x not in seen

    out = set()
    for k, i in enumerate(body):
        if i.kind != "br" or i.target is None or i.target >= i.addr or i.target not in at:
            continue
        t = at[i.target]
        if body[t].kind == "rep" and body[t].table and i.addr == body[t].table[0]:
            continue                # (if (rN != 0) goto rep: an interrupted rep resumed, not a loop)
        seen, todo = {t}, [t]       # a cycle inside the span: the branch reachable from its target through the
        while todo and k not in seen:   # code between them (not code two paths share, a block out of line)
            for y in succ[todo.pop()]:
                if t <= y <= k and y not in seen:
                    seen.add(y)
                    todo.append(y)
        if k in seen and (dominates(t, k) or dominates(k, t)):  # (k over t: a loop entered at its test)
            out.add(i.addr)
    return out


def loops(insns):
    """[(head, end)]: one span per loop head, closed by its farthest backward branch that closes a loop"""
    if not insns:
        return []
    lo = insns[0].addr
    heads = {}
    edges = back_edges(insns)
    for i in insns:
        if i.addr in edges and lo <= i.target < i.addr:
            heads[i.target] = max(heads.get(i.target, i.addr), i.addr)
        if i.kind == "rep":         # a rep body: a loop of its own, closed without a branch
            last = [x.addr for x in insns if i.addr < x.addr < i.table[0]]
            if last:
                heads[i.addr + i.size] = max(last)
    return sorted(heads.items())


# branch directions to follow instead of the walk's own (hex addresses, comma-separated): the path of a given case
FORCE = {int(a, 16): tk for v, tk in (("PI32_TAKE", True), ("PI32_FALL", False))
         for a in os.environ.get(v, "").split(",") if a}


def path(insns, h, e, spans):
    """the straight path of one iteration of the loop (h, e): forward branches not taken, an unconditional jump
    inside the span followed, the back edge taken. Returns [(Insn, taken)] and the inner loops passed"""
    idx = {i.addr: k for k, i in enumerate(insns)}
    k = idx.get(h)
    out, inner, steps = [], [], 0
    while k is not None and k < len(insns) and steps < 4000:
        steps += 1
        i = insns[k]
        if i.nst == -1:         # a bundle tail: walked with its head
            k += 1
            continue
        if i.addr > e:
            break
        for (h2, e2) in spans:      # an inner loop's head: noted (its back edge is not taken on this path)
            if h2 == i.addr and (h2, e2) != (h, e) and h < h2 <= e2 <= e and (h2, e2) not in inner:
                inner.append((h2, e2))
        if i.kind == "br" and i.target is not None and i.cond and i.addr in FORCE:
            out.append((i, FORCE[i.addr]))      # (a direction given: PI32_TAKE / PI32_FALL)
            k = idx.get(i.target) if FORCE[i.addr] else k + 1
            if k is None:
                return out, inner
            continue
        if i.kind == "br" and i.target is not None:
            if i.target == h and (i.addr == e or not i.cond):
                out.append((i, True))
                return out, inner
            if i.cond and i.target > i.addr and h <= i.target <= e and any(
                    x.kind == "div" for x in insns[k + 1:idx.get(i.target, k + 1)]):
                out.append((i, True))       # a guard around a divide (a polyBLEP edge, a zero test): taken, the
                k = idx.get(i.target)       # divide rare (the code's own rule: none per sample)
                continue
            if i.cond:
                out.append((i, False))
                k += 1
                continue
            if h <= i.target <= e and i.target > i.addr:      # a jump forward inside the loop
                out.append((i, True))
                k = idx.get(i.target)
                continue
            out.append((i, True))       # out of the loop, or back to an outer head: the iteration ends
            return out, inner
        if i.kind == "jt" and i.target is not None and h <= i.target <= e:
            out.append((i, True))
            k = idx.get(i.target)
            continue
        if i.kind in ("ret", "jt"):
            out.append((i, True))
            return out, inner
        out.append((i, i.kind == "call"))
        k += 1
    return out, inner


class Sim:
    """a scoreboard over one path, repeated: register ready times (for an ALU use, for an address), the memory
    port, the store run; stalls charged to their cause"""

    def __init__(self):
        self.t = 0.0
        self.rdy = {}           # reg -> (alu ready, address ready)
        self.port = 0.0
        self.run = 0            # stores in a row
        self.stall = {}
        self.prev48 = False

    def need(self, i, mem):
        """the earliest issue of i and its cause"""
        best, why = self.t, None
        for r in i.src:
            a = self.rdy.get(r, (0, 0, ""))
            if a[0] > best:
                best, why = a[0], a[2] + "-use"
        for r in i.asrc:
            a = self.rdy.get(r, (0, 0, ""))
            if a[1] > best:
                best, why = a[1], a[2] + "-addr"
        if mem and self.port > best:
            best, why = self.port, "mem-port"
        return best, why

    def put(self, regs_, alu, kind):
        for r in regs_:
            self.rdy[r] = (alu, alu + 2, kind)

    def charge(self, why, c):
        if c > 0 and why:
            self.stall[why] = self.stall.get(why, 0) + c

    def step(self, i, taken, nxt48, taken_cost, misalign):
        halves = [i] + ([i.bundle] if i.bundle is not None else [])
        mem = any(x.mem() for x in halves)
        start, why = self.t, None
        for x in halves:
            s, w = self.need(x, x.mem())
            if s > start:
                start, why = s, w
        self.charge(why, start - self.t)
        occ = 1.0
        stores = 0
        for n, x in enumerate(halves):
            k = x.kind
            o = 1.0
            if k == "ld":
                self.port = start + 2
                self.put(x.dst, start + 3, "load")
                self.rdy.update({r: (start + 1, start + 1, "wb") for r in x.wb})   # forwarded (fm6_op_run's 40)
                o = 2.0 if n == 1 else 1.0          # a load in a bundle's tail: 2
            elif k == "ld64":
                self.port = start + 4
                if x.dst:
                    self.put(x.dst[1:], start + 3, "load")      # rHI_rLO: the low one as a 32-bit load's
                    self.put(x.dst[:1], start + 4, "load")      # value, the high one 1 later (fits dv_metal_run's 35)
                self.rdy.update({r: (start + 1, start + 1, "wb") for r in x.wb})
                o = 1.0
            elif k == "pop":
                self.port = start + 1 + len(x.dst)
                self.put(x.dst, self.port + 1, "load")
                o = self.port - start
            elif k == "st":
                self.port = start + 1
                self.rdy.update({r: (start + 1, start + 1, "wb") for r in x.wb})
                stores += 1
            elif k == "push":
                c = x.nst + (x.nst - 1) // 2
                self.port = start + c
                o = c
            elif k == "rmw":
                self.port = start + 3
                o = 3.0
            elif k == "mul":
                self.put(x.dst, start + 3, "mul")
                o = 2.0 if n == 0 else 1.0
            elif k == "short":
                self.put(x.dst, start + 2, "short")
            elif k == "rep":
                o = REP_SETUP
            elif k == "div":
                o = DIV_CYC
                self.put(x.dst, start + DIV_CYC, "div")
            else:
                self.put(x.dst, start + 1, "alu")
            occ = max(occ, o)
        if stores:
            self.run += 1
            if self.run >= 3 and self.run % 2 == 1:     # n + (n - 1) // 2: every second store after the first
                occ += 1
                self.charge("store-run", 1)
        else:
            self.run = 0
        is48 = i.size == 6
        if is48 and (self.prev48 or nxt48) and occ < 1.5:
            self.charge("48-bit", 1.5 - occ)
            occ = 1.5
        self.prev48 = is48
        if taken:
            c = taken_cost(i) + misalign(i)
            self.charge("branch", c - 1)
            if i.kind == "call":
                c = taken_cost(i) + CALL_RET - TAKEN
            occ = max(occ, c)
        self.t = start + occ
        return start


def estimate(insns, h, e, spans=None, cache=None):
    """the loop (h, e): its cycles an iteration in the steady state (inner loops run NEST times), and details"""
    spans = spans if spans is not None else loops(insns)
    cache = cache if cache is not None else {}
    if (h, e) in cache:
        return cache[(h, e)]
    cache[(h, e)] = {"cyc": 0, "insns": 0, "ins_all": 0, "targets": set()}      # (a cycle in the spans)
    p, inner = path(insns, h, e, spans)
    rep = any(i.kind == "rep" and i.addr + i.size == h for i in insns)
    if rep:                 # a rep body: each instruction once, no branch back (the hardware loops it)
        p = [(i, False) for i in insns if h <= i.addr <= e and i.nst != -1]
        inner = []
    linear = not rep and (not p or p[-1][0].target != h)
    if linear:      # the walk left the loop (a return, a jump out): every instruction of the span once instead
        p = [(i, i.addr == e or (i.kind in ("br", "jt") and not i.cond)) for i in insns
             if h <= i.addr <= e and i.nst != -1]
        inner = [(h2, e2) for h2, e2 in spans if h < h2 <= e2 <= e]
    at = {i.addr: i for i in insns}
    targets = set()
    for i, tk in p:
        if tk:
            if i.target is not None:
                targets.add(i.target)
            if i.kind == "call":
                targets.add(i.addr + i.size)
    sub = [estimate(insns, h2, e2, spans, cache) for h2, e2 in inner]
    for s in sub:
        targets |= s["targets"]
    nt = len(targets)

    def taken_cost(i):
        return COLD if nt >= 5 else TAKEN

    def misalign(i):
        tg = i.target if i.kind != "call" else None
        x = at.get(tg) if tg is not None else None
        return 1 if (x is not None and tg % 4 == 2 and x.size >= 4) else 0

    sim = Sim()
    marks = []
    for rep in range(3):
        marks.append(sim.t)
        for n, (i, tk) in enumerate(p):
            nxt = p[n + 1][0] if n + 1 < len(p) else p[0][0]
            sim.step(i, tk, nxt.size == 6, taken_cost, misalign)
        if rep == 0:
            sim.stall = {}
    marks.append(sim.t)
    cyc = (marks[3] - marks[1]) / 2
    stall = {k: v / 2 for k, v in sim.stall.items()}
    if nt == 4:             # two of the four targets miss each iteration
        cyc += 2 * (COLD - TAKEN)
        stall["branch"] = stall.get("branch", 0) + 2 * (COLD - TAKEN)
    n_self = sum(1 + (i.bundle is not None) for i, _ in p)
    cyc_all = cyc + sum((NEST - 1) * s["cyc"] for s in sub)
    ins_all = n_self + sum((NEST - 1) * s["ins_all"] for s in sub)
    head = at.get(h)
    r = {"head": h, "end": e, "cyc_iter": cyc, "linear": linear, "cyc": cyc_all, "insns": n_self, "ins_all": ins_all, "targets": targets, "ntargets": nt,
         "stall": stall, "inner": inner,
         "misaligned": bool(head is not None and h % 4 == 2 and head.size >= 4),
         "div": sum(i.kind == "div" or (i.bundle is not None and i.bundle.kind == "div") for i, _ in p),
         "calls": [i.text for i, _ in p if i.kind == "call"],
         "i48": sum(i.size == 6 for i, _ in p),
         "maxrun": maxrun(p)}
    cache[(h, e)] = r
    return r


def maxrun(p):
    best = run = 0
    for i, _ in p:
        st = i.kind in ("st", "push") or (i.bundle is not None and i.bundle.kind == "st")
        run = run + 1 if st else 0
        best = max(best, run)
    return best


def function(insns):
    """every loop of a function, and its cycle cost: the top-level loops' iterations (inner ones x NEST), the
    counterpart of target_budget.py's static cost"""
    spans = loops(insns)
    cache = {}
    res = [estimate(insns, h, e, spans, cache) for h, e in spans]
    top = [r for r in res if not any(h2 < r["head"] and r["end"] <= e2 and (h2, e2) != (r["head"], r["end"])
                                     for h2, e2 in spans)]
    return {"loops": res, "cyc": sum(r["cyc"] for r in top), "ins": sum(r["ins_all"] for r in top),
            "linear": straight(insns)}


def straight(insns):
    """(cycles, instructions) of the whole function run once in address order (forward branches not taken):
    the cycles per instruction of code without loops"""
    p = [(i, i.kind in ("br", "jt") and not i.cond) for i in insns if i.nst != -1]
    sim = Sim()
    for n, (i, tk) in enumerate(p):
        sim.step(i, tk, n + 1 < len(p) and p[n + 1][0].size == 6, lambda i: TAKEN, lambda i: 0)
    return sim.t, sum(1 + (i.bundle is not None) for i, _ in p)


# (published rule, instructions, cycles) of #283: the model's own check (target_budget.py, with the two loops)
MICRO = [("load, independent ALU, load", ["r1 = [r0+0]", "r2 = r3 + r4", "r5 = [r0+4]"], 4),
         ("two loads", ["r1 = [r0+0]", "r5 = [r0+4]"], 4),
         ("64-bit load", ["r5_r4 = d[r0+0]"], 4),
         ("ALU result as the next address: +2", ["r1 = r2 + r3", "r4 = [r1+0]"], 5),
         ("ALU result as an address 2 later: +1", ["r1 = r2 + r3", "r6 = r7 + r8", "r4 = [r1+0]"], 5),
         ("3 stores in a row: n + (n - 1) // 2", ["[r0+0] = r1", "[r0+4] = r2", "[r0+8] = r3"], 4),
         ("5 stores in a row", ["[r0+0] = r1", "[r0+4] = r2", "[r0+8] = r3", "[r0+12] = r4", "[r0+16] = r5"], 7),
         ("3 stores, an ALU op between each", ["[r0+0] = r1", "r5 = r6 + r7", "[r0+4] = r2", "r8 = r6 + r7",
                                               "[r0+8] = r3"], 5),
         ("[rB+imm] += rV", ["[r6+0] += r4"], 3),
         ("[rB+imm] += rV right after its base", ["r6 = r7 + r8", "[r6+0] += r4"], 6),
         ("multiply, its product in the next op", ["r1 *= r2", "r3 = r1 + r4"], 4),
         ("smin, its result in the next op", ["r1 = smin(r2, r3)", "r4 = r1 + r5"], 3)]


def micro():
    """[(rule, cycles by the model, cycles by #283)] of MICRO: the straight sequences, from the first issue to the
    last instruction done (its memory port free)"""
    out = []
    for name, seq, want in MICRO:
        sim = Sim()
        for k, t in enumerate(seq):
            sim.step(Insn(4 * k, 4, t), False, False, lambda i: TAKEN, lambda i: 0)
        out.append((name, max(sim.t, sim.port), want))
    return out


def hazards(r):
    out = []
    for k, v in sorted(r["stall"].items(), key=lambda kv: -kv[1]):
        if v >= 0.5:
            out.append(f"{k} {v:g}")
    if r["div"]:
        out.append(f"{r['div']} div")
    if r["ntargets"] > 3:
        out.append(f"{r['ntargets']} taken targets")
    if r["misaligned"]:
        out.append("head 2 mod 4")
    if r["maxrun"] >= 3:
        out.append(f"store run {r['maxrun']}")
    if r["i48"] >= 2:
        out.append(f"{r['i48']} x 48-bit")
    if r["linear"]:
        out.append("(linear)")
    if r["calls"]:
        out.append(f"{len(r['calls'])} call")
    return out


def report(fns, names):
    for n in names:
        if n not in fns:
            print(f"{n}: not found")
            continue
        f = function(fns[n])
        print(f"{n}: ~{f['cyc']:.0f} cycles (top-level loops, inner x{NEST})")
        for r in f["loops"]:
            print(f"  loop {r['head']:x}..{r['end']:x}: {r['insns']:3d} insns, {r['cyc_iter']:6.1f} cyc/iter "
                  f"({r['cyc_iter'] / max(r['insns'], 1):.2f}/insn)  " + ", ".join(hazards(r)))


if __name__ == "__main__":
    a = sys.argv[1:]
    dis = a.pop(0) if a and a[0].endswith(".dis") else "build/felucca.dis"
    report(parse(dis, set(a) if a else None), a)
