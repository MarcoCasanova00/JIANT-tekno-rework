/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* (JIANT 0.9.12) Safe by default. Some FM-1s crash now and then with both cores at work (the second core's misreads:
 * hal/fm1_multicore.h), and JieLi rates the chip for 320 MHz where the boot stage runs it at 360. So:
 *   MENU > SYSTEM > CPU        2 CORES (the default, as 0.6.71 .. 0.9.11) or 1 CORE. A crash or a hang (the watchdog)
 *                              while both cores ran, or the second core given up (a misread, no answer, a fault), turns
 *                              it to 1 CORE for good (CPU_AUTO: ABOUT and the boot's notice say why); the user may set
 *                              2 CORES again. From the next start (main.c), as the clock
 *   MENU > SYSTEM > CPU CLOCK  360 MHZ (the default) or 320 MHZ, switched at boot before the audio and the second core
 *                              (main.c, hal/fm1_clk320.h: curist's, MIT); from the next power-on (a soft reset keeps the
 *                              PLL)
 *   the last crash             kept in the settings record (crash_log: SLICE's retired favourites row, 32 bytes), so a
 *                              power-off does not lose it: MENU > ABOUT shows it; the editor's backup carries it (the
 *                              settings record, EDITOR_PROTOCOL.md). tools/crash_decode.py reads its numbers
 * The settings byte: favorites.factory[15][17] (a byte no engine uses), 0 in every older record = 2 CORES, 360 MHZ */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "0"                     /* (a host test without felucca.c) */
#endif
#define cpu_cfg (favorites.factory[15][17])
enum { CPU_1CORE = 1u, CPU_AUTO = 2u, CPU_320 = 4u, CPU_WAIT = 8u | 16u, CPU_GOOD = 32u, CPU_TRIED = 64u, CPU_V2 = 128u };   /* (0.9.13:
                                                         * WAIT the power-ons on one core after a crash, 0..2; GOOD for
                                                         * good; TRIED both cores again after one) */
/* a crash's flags (main.c fm1_fault, cpu_boot): both cores ran, a hang (the watchdog), 320 MHz */
enum { CR_2C = 1u, CR_WDT = 4u, CR_320 = 8u };
/* the record: 32 characters, as MENU > ABOUT shows them, "IL01 2C 0.9.12 0201ABCD 0201ABCD": the short code and the
 * vector (crash_code; WDFF a hang), how many cores ran, the firmware, PC, RETS (a hang: the main loop's stage and page) */
#define CRASH_LOG ((char *)favorites.factory[13])   /* (13: SLICE's, retired; nothing reads its stars) */
#define CRASH_LOG_N 32u
/* (0.9.19) more of the last crash, in the favourites rows of two more retired engines (no star of theirs is ever read or
 * offered: the editor's FAV_GET / FAV_SET stop at an engine's presets, and these have none), and the crash before:
 *   CRASH_DET   GRAIN's row (8):  "JD", the crashes logged (u16), the core, the flags (CR_*, 16 the audio interrupt was
 *               running, 32 the second core's PC read), the main loop's stage and page, SP, EMU, DBG, the time since the
 *               start (ms), the last two branches (ETM)
 *   CRASH_DET2  TRIO's row (6):   "JE", the clock as the boot stage left it (hal/fm1_clk320.h's check: 0 360 MHz from
 *               the V15 boot stage, 1 already 320, 2.. other), PLL2's multiplier - 200, the second core's PC, the debug
 *               unit's culprit registers (PRP_WR_LIMIT_ID, AXI_RD_INV_ID, AXI_WR_INV_ID, PRP_MMU_ERR_WID / RID: the bus
 *               master that made the bad access), the three write-limit windows' numbers (a byte each)
 *   CRASH_PREV  PHYS's row (9):   the crash before, its line as CRASH_LOG had it
 * MENU > ABOUT shows them under LAST CRASH; tools/crash_decode.py --line / --det reads them (and a settings backup) */
#define CRASH_DET (favorites.factory[8])
#define CRASH_DET2 (favorites.factory[6])
#define CRASH_PREV ((char *)favorites.factory[9])
enum { CRD_AUDIO = 16u, CRD_OTHER = 32u };
typedef struct {
    uint32_t core, flags, stage, page, sp, emu, dbg, up_ms, etm0, etm1;
    uint32_t clk, mult, other_pc, bus[8];
} crash_det_t;
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t crash_count(void)               /* the crashes and hangs logged (CRASH_DET), 0 none */
{
    const uint8_t *d = CRASH_DET;
    return d[0] == 'J' && d[1] == 'D' ? (uint32_t)(d[2] | d[3] << 8) : 0u;
}
/* the detail of the crash just logged (after crash_log_put): the count goes on */
static void crash_det_put(const crash_det_t *c)
{
    uint8_t *d = CRASH_DET, *e = CRASH_DET2;
    uint32_t n = crash_count() + 1u, i;
    if (n > 0xFFFFu)
        n = 0xFFFFu;
    d[0] = 'J'; d[1] = 'D'; d[2] = (uint8_t)n; d[3] = (uint8_t)(n >> 8);
    d[4] = (uint8_t)c->core; d[5] = (uint8_t)c->flags; d[6] = (uint8_t)c->stage; d[7] = (uint8_t)c->page;
    put32(d + 8, c->sp); put32(d + 12, c->emu); put32(d + 16, c->dbg); put32(d + 20, c->up_ms);
    put32(d + 24, c->etm0); put32(d + 28, c->etm1);
    e[0] = 'J'; e[1] = 'E'; e[2] = (uint8_t)c->clk; e[3] = (uint8_t)(c->mult - 200u);
    put32(e + 4, c->other_pc);
    for (i = 0; i < 5u; i++)
        put32(e + 8 + 4 * i, c->bus[3 + i]);
    e[28] = (uint8_t)c->bus[0]; e[29] = (uint8_t)c->bus[1]; e[30] = (uint8_t)c->bus[2]; e[31] = 0;
}
static int crash_det_get(crash_det_t *c)        /* 1: CRASH_DET (and CRASH_DET2 if there) read */
{
    const uint8_t *d = CRASH_DET, *e = CRASH_DET2;
    uint32_t i;
    if (!crash_count())
        return 0;
    memset(c, 0, sizeof *c);
    c->core = d[4]; c->flags = d[5]; c->stage = d[6]; c->page = d[7];
    c->sp = get32(d + 8); c->emu = get32(d + 12); c->dbg = get32(d + 16); c->up_ms = get32(d + 20);
    c->etm0 = get32(d + 24); c->etm1 = get32(d + 28);
    if (e[0] == 'J' && e[1] == 'E') {
        c->clk = e[2]; c->mult = e[3] + 200u; c->other_pc = get32(e + 4);
        for (i = 0; i < 5u; i++)
            c->bus[3 + i] = get32(e + 8 + 4 * i);
        c->bus[0] = e[28]; c->bus[1] = e[29]; c->bus[2] = e[30];
    }
    return 1;
}
/* the crash before (CRASH_PREV), as crash_log_get */
static int crash_prev_get(char *b)
{
    uint32_t i;
    memcpy(b, CRASH_PREV, CRASH_LOG_N);
    b[CRASH_LOG_N] = 0;
    for (i = 0; i < CRASH_LOG_N; i++)
        if (b[i] < ' ' || b[i] > 'Z')
            return 0;
    return b[4] == ' ' && b[6] == 'C' && b[7] == ' ' && b[0] >= '?' && b[0] <= 'Z';
}

#if FELUCCA_DUAL_CORE
#define cpu_is_320() fm1_clk_320()             /* (hal/fm1_sys.h: the PLL now) */
#else
#define cpu_is_320() 0                          /* (the host, the emulator: 360) */
#endif

static char *hex8(char *b, uint32_t v)          /* 8 hex digits and a 0; the end */
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
    return b + 8;
}

/* the crash's short code, two letters (MANUAL: Troubleshooting): the first cause found, EMU first (MA misaligned, IL
 * illegal instruction, DZ divide by 0, ST stack), then DBG (PC a fetch outside the code, WR a protected write, MM the
 * MMU, BU the bus: an unmapped address), WD a hang, ?? none */
/* (0.9.19) and DBG's bit 0, the watchdog's timeout raised as the exception: a hang caught with a red screen, WD too (the
 * AC79 SDK: wdt_timout_err), before the other DBG bits */
static void crash_code(char *b, uint32_t emu, uint32_t dbg, uint32_t flags)
{
    static const char C[] = "MAILDZSTPCWRMMBU??WD";
    static const uint32_t M[4] = {(1u << 10) | (1u << 12), (1u << 2) | (1u << 11) | (1u << 13),
                                  (1u << 1) | (1u << 3) | (15u << 6), (3u << 4) | (63u << 16)};
    uint32_t k = 0;
    if ((flags & CR_WDT) || (!(emu & 15u) && (dbg & 1u)))
        k = 9;
    else {
        while (k < 4u && !((emu >> k) & 1u))
            k++;
        while (k >= 4u && k < 8u && !(dbg & M[k - 4u]))
            k++;
    }
    b[0] = C[2u * k];
    b[1] = C[2u * k + 1u];
    b[2] = 0;
}

/* the record for a crash (or a hang) the last run left; the caller saves the settings */
static int crash_log_get(char *b);
static void crash_log_put(uint32_t flags, uint32_t vec, uint32_t pc, uint32_t rets, uint32_t emu, uint32_t dbg)
{
    char b[48], *p = b;
    if (crash_log_get(b))                       /* (0.9.19) the one before kept (PHYS's retired row): CRASH_PREV */
        memcpy(CRASH_PREV, CRASH_LOG, CRASH_LOG_N);
    crash_code(p, emu, dbg, flags);
    hex8(p + 2, vec);
    p[2] = p[8];
    p[3] = p[9];
    p[4] = ' ';
    p[5] = flags & CR_2C ? '2' : '1';
    p[6] = 'C';
    p[7] = ' ';
    str_cpy(p + 8, FELUCCA_VERSION + (FELUCCA_VERSION[0] == 'v'), 7);
    p += str_len(p);
    *p++ = ' ';
    p = hex8(p, pc);
    *p++ = ' ';
    hex8(p, rets);
    memcpy(CRASH_LOG, b, CRASH_LOG_N);
}
/* MENU > ABOUT's line (b: CRASH_LOG_N + 1); 0 = none kept */
static int crash_log_get(char *b)
{
    memcpy(b, CRASH_LOG, CRASH_LOG_N);
    b[CRASH_LOG_N] = 0;
    return b[4] == ' ' && b[6] == 'C' && b[7] == ' ' && b[0] >= '?' && b[0] <= 'Z';
}

/* (0.9.19) MENU > ABOUT's lines under LAST CRASH (b: 48 bytes): line i (0..CRASH_LINES - 1) of the detail, 0 = not
 * shown (none kept, or nothing to say there). Short, every number
 * in hex as tools/crash_decode.py --det takes them:
 *   "CRASHES 3  CORE 0  UP 754 S"           how many logged, the core (- a hang), the time since the start
 *   "SP 01C79E40  AT AUDIO"                 the stack pointer; AT AUDIO (the audio interrupt ran) or UI s.p (the main
 *                                           loop's stage: 1 input, 2 drawing, 9 waiting; the page)
 *   "EMU 00000000 DBG 00001000"
 *   "C2 PC 0200F2A4"                        the second core's PC (when it ran)
 *   "BUS 00000100 00000000 00000000"        the bad access's masters: read, write, write limit (when any)
 *   "CLK BOOT 0 PLL 270"                    the boot stage's clock (0 V15's 360, 1 at 320 already, 2.. another), PLL2 */
#define CRASH_LINES 6u
static char *dec_u(char *b, uint32_t v)
{
    char t[12];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10u);
    while (n) *b++ = t[--n];
    *b = 0;
    return b;
}
static int crash_about_line(uint32_t i, char *b)
{
    crash_det_t c;
    char *p = b;
    if (!crash_det_get(&c))
        return 0;
    switch (i) {
    case 0:
        str_cpy(p, "CRASHES ", 9); p = dec_u(p + 8, crash_count());
        str_cpy(p, "  CORE ", 8); p += 7;
        *p++ = c.core > 1u ? '-' : (char)('0' + c.core);
        str_cpy(p, "  UP ", 6); p = dec_u(p + 5, c.up_ms / 1000u);
        str_cpy(p, " S", 3);
        return 1;
    case 1:
        str_cpy(p, "SP ", 4); p = hex8(p + 3, c.sp);
        if (c.flags & CRD_AUDIO) {
            str_cpy(p, "  AT AUDIO", 11);
        } else {
            str_cpy(p, "  UI ", 6); p = dec_u(p + 5, c.stage);
            *p++ = '.';
            dec_u(p, c.page);
        }
        return 1;
    case 2:
        str_cpy(p, "EMU ", 5); p = hex8(p + 4, c.emu);
        str_cpy(p, " DBG ", 6); hex8(p + 5, c.dbg);
        return 1;
    case 3:
        if (!(c.flags & CRD_OTHER))
            return 0;
        str_cpy(p, "C2 PC ", 7); hex8(p + 6, c.other_pc);
        return 1;
    case 4:
        if (!(c.bus[4] | c.bus[5] | c.bus[3]))
            return 0;
        str_cpy(p, "BUS ", 5); p = hex8(p + 4, c.bus[4]);
        *p++ = ' '; p = hex8(p, c.bus[5]);
        *p++ = ' '; hex8(p, c.bus[3]);
        return 1;
    case 5:
        str_cpy(p, "CLK BOOT ", 10); p = dec_u(p + 9, c.clk);
        str_cpy(p, " PLL ", 6); dec_u(p + 5, c.mult);
        return 1;
    default:
        return 0;
    }
}

/* the boot's CPU setting after the last run: a crash or a hang while both cores ran turns 2 CORES off; (0.9.13) for
 * three power-ons, then both cores again (cpu_retry: a crash in 0.9.12 left a unit on one core for good, every song past
 * four tracks over its CPU); a crash again after that retry: for good (until MENU > SYSTEM > CPU) */
static uint32_t cpu_after(uint32_t cfg, uint32_t crash_flags)
{
    if (cfg & CPU_1CORE || !(crash_flags & CR_2C))
        return cfg;
    return (cfg & ~(uint32_t)CPU_WAIT) | CPU_1CORE | CPU_AUTO | (cfg & CPU_TRIED ? CPU_GOOD : 0u);
}
/* (0.9.19) CPU CLOCK 320 MHZ the default: M-VAVE's own open firmware (fm1_open V14) runs the chip at 320 MHz, JieLi's
 * rated clock, where V15's boot stage sets 360 (not on JieLi's list); the second core's misreads and the crashes beside
 * it look like that margin. Once, every record without CPU_V2 (all before 0.9.19, 360 their default) to 320; MENU >
 * SYSTEM > CPU CLOCK 360 MHZ sets it back for good. ~11 % fewer cycles a block (voices shed sooner on a heavy song) */
static uint32_t cpu_migrate(uint32_t cfg) { return cfg & CPU_V2 ? cfg : cfg | CPU_V2 | CPU_320; }
/* a power-on with no crash before it: one on one core after a crash counted, the third gives both cores back */
static uint32_t cpu_retry(uint32_t cfg)
{
    uint32_t w = (cfg & CPU_WAIT) >> 3;
    if ((cfg & (CPU_1CORE | CPU_AUTO | CPU_GOOD)) != (CPU_1CORE | CPU_AUTO))
        return cfg;
    if (w < 2u)
        return (cfg & ~(uint32_t)CPU_WAIT) | (w + 1u) << 3;
    return (cfg & ~(uint32_t)(CPU_WAIT | CPU_1CORE | CPU_AUTO)) | CPU_TRIED;
}

/* 2 CORES off for good (cpu_after), saved, and said: why, then where to set it back (main.c: after a crash at boot, the
 * second core given up while it plays) */
static __attribute__((noinline)) void cpu_off(const char *why)
{
    cpu_cfg = (uint8_t)cpu_after(cpu_cfg, CR_2C);
    settings_save();
    ui_message(why);
    str_cpy(ui.msg2, "MENU > SYSTEM > CORES", sizeof ui.msg2);   /* (0.9.18: CPU CORES, was CPU) */
}

static const char CPU_APPLY[] = "SWITCH OFF AND ON TO APPLY";
static const char *const CPU_N[2][2] = {{"1 CORE", "2 CORES"}, {"360 MHZ", "320 MHZ"}};
/* MENU > SYSTEM > CPU (clock 0) / CPU CLOCK (clock 1): the value as the menu steps it */
static uint32_t cpu_get(uint32_t clock) { return clock ? (cpu_cfg & CPU_320) != 0u : !(cpu_cfg & CPU_1CORE); }
static void cpu_put(uint32_t clock, uint32_t v)   /* (from the next start: main.c) */
{
    if (v != cpu_get(clock))
        ui_message(CPU_APPLY);
    cpu_cfg = (uint8_t)(clock ? (v ? cpu_cfg | CPU_320 : cpu_cfg & ~CPU_320) :
                        (v ? cpu_cfg & ~CPU_1CORE : cpu_cfg | CPU_1CORE) & ~(CPU_AUTO | CPU_WAIT | CPU_GOOD | CPU_TRIED));
}
/* MENU > ABOUT: the cores (fx.c mc_status, and why one: the MENU or a crash), the clock */
static const char *cpu_status(void)
{
    if (!mc_live() && (cpu_cfg & CPU_1CORE))     /* (1 CORE set: core 1 never started) */
        return cpu_cfg & CPU_AUTO ? (cpu_cfg & CPU_GOOD ? "CORES 1 (AFTER 2 CRASHES)" : "CORES 1 (AFTER A CRASH)") : "CORES 1 (MENU > CORES)";
    return mc_status();
}
