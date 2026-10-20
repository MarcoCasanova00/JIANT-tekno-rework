/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* (JIANT 0.9.12) firmware/src/cpu_safe.c on the host: the crash's short code, the record kept in the settings (its
 * text, what is not one), the fallback to 1 CORE after a crash beside the second core, MENU > SYSTEM > CPU / CPU CLOCK's
 * values, messages and the ABOUT line. The boot side (main.c cpu_boot) is these calls in order: modelled at the end */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define FELUCCA_VERSION "v9.9.12"
static struct { uint8_t factory[16][32]; uint32_t user, filter; } favorites;
static struct { char msg[40], msg2[40]; } ui;
static int saves, mc_host;
static unsigned str_len(const char *s) { return (unsigned)strlen(s); }
static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i + 1 < max) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
static void ui_message(const char *s) { str_cpy(ui.msg, s, sizeof ui.msg); ui.msg2[0] = 0; }
static void settings_save(void) { saves++; }
#define mc_live() (mc_host != 0)
static const char *mc_status(void) { return mc_host ? "CORES 2" : "CORES 1"; }
#include "../firmware/src/cpu_safe.c"

static int ok_all = 1;
static void check(const char *what, int ok)
{
    printf("cpu_safe: %-78s %s\n", what, ok ? "ok" : "FAILED");
    ok_all &= ok;
}
static int code_is(uint32_t emu, uint32_t dbg, uint32_t fl, const char *want)
{
    char b[3];
    crash_code(b, emu, dbg, fl);
    return !strcmp(b, want);
}

int main(void)
{
    char b[CRASH_LOG_N + 1u];
    uint32_t k;
    check("the short code: EMU's misaligned, illegal, divide by 0, stack (MA IL DZ ST)",
          code_is(1, 0, 0, "MA") && code_is(2, 0, 0, "IL") && code_is(4, 0, 0, "DZ") && code_is(8, 0, 0, "ST"));
    check("  DBG's PC limit (both cores), protected writes, MMU, bus (PC WR MM BU)",
          code_is(0, 1u << 10, 0, "PC") && code_is(0, 1u << 12, 0, "PC") && code_is(0, 1u << 13, 0, "WR") &&
          code_is(0, 1u << 2, 0, "WR") && code_is(0, 1u << 7, 0, "MM") && code_is(0, 1u << 21, 0, "BU") &&
          code_is(0, 1u << 4, 0, "BU"));
    check("  EMU before DBG; nothing set ??; a hang WD whatever the bits",
          code_is(2, 1u << 21, 0, "IL") && code_is(0, 1u << 14, 0, "??") && code_is(0, 0, 0, "??") &&
          code_is(2, 1u << 21, CR_WDT, "WD"));

    check("no record in a new settings record (zeros), nor in SLICE's old stars", !crash_log_get(b) &&
          (memset(favorites.factory[13], 0xA5, 32), !crash_log_get(b)) && (memset(favorites.factory[13], 0, 32), 1));
    favorites.factory[12][31] = 0x11;
    favorites.factory[14][0] = 0x22;
    crash_log_put(CR_2C, 1, 0x0201ABCDu, 0x02012345u, 2u, 0u);
    check("a crash beside the second core: \"IL01 2C 9.9.12 0201ABCD 02012345\"",
          crash_log_get(b) && !strcmp(b, "IL01 2C 9.9.12 0201ABCD 02012345"));
    crash_log_put(CR_WDT, 0xFFu, 2u, 7u, 0u, 0u);
    check("a hang on one core: \"WDFF 1C 9.9.12 00000002 00000007\" (the stage, the page)",
          crash_log_get(b) && !strcmp(b, "WDFF 1C 9.9.12 00000002 00000007"));
    check("  the record is the row's 32 bytes, the settings around it untouched",
          favorites.factory[12][31] == 0x11 && favorites.factory[14][0] == 0x22);

    check("fallback: a crash with 2 CORES on turns it to 1 CORE (AFTER A CRASH), the clock kept",
          cpu_after(CPU_320, CR_2C) == (CPU_320 | CPU_1CORE | CPU_AUTO) && cpu_after(0, CR_2C | CR_WDT) == (CPU_1CORE | CPU_AUTO));
    check("  a crash on one core, or already 1 CORE (set by hand): unchanged",
          cpu_after(0, 0) == 0 && cpu_after(CPU_1CORE, CR_2C) == CPU_1CORE && cpu_after(CPU_320, CR_WDT) == CPU_320);
    {   /* (0.9.13) the fallback lasts three power-ons, then both cores again; a crash after that retry: for good */
        uint32_t c = cpu_after(CPU_320, CR_2C), c1 = cpu_retry(c), c2 = cpu_retry(c1), c3 = cpu_retry(c2), c4 = cpu_after(c3, CR_2C);
        check("(0.9.13) after a crash: 1 CORE for two clean power-ons, the third both cores (the clock kept)",
              (c1 & CPU_1CORE) && (c2 & CPU_1CORE) && !(c3 & (CPU_1CORE | CPU_AUTO)) && (c3 & CPU_TRIED) && (c3 & CPU_320));
        check("  a crash with both cores again after the retry: 1 CORE for good (no retry), MENU > CPU clears it all",
              (c4 & (CPU_1CORE | CPU_AUTO | CPU_GOOD)) == (CPU_1CORE | CPU_AUTO | CPU_GOOD) && cpu_retry(c4) == c4 &&
              cpu_retry(CPU_1CORE) == CPU_1CORE && cpu_retry(0) == 0);
        cpu_cfg = (uint8_t)c4; cpu_put(0, 1);
        check("  MENU > SYSTEM > CPU 2 CORES by hand: the crash state gone", cpu_cfg == CPU_320);
    }

    cpu_cfg = 0;
    mc_host = 1;
    check("MENU > SYSTEM > CPU: 2 CORES by default (0 in older settings), CPU CLOCK 360 MHZ",
          cpu_get(0) == 1u && cpu_get(1) == 0u && !strcmp(CPU_N[0][cpu_get(0)], "2 CORES") &&
          !strcmp(CPU_N[1][cpu_get(1)], "360 MHZ"));
    ui.msg[0] = 0;
    cpu_put(0, 0);
    check("  1 CORE: set, from the next start (said)", cpu_cfg == CPU_1CORE && !strcmp(ui.msg, CPU_APPLY));
    ui.msg[0] = 0;
    cpu_put(0, 0);
    check("  the same value again: nothing said", cpu_cfg == CPU_1CORE && !ui.msg[0]);
    cpu_put(1, 1);
    check("  CPU CLOCK 320 MHZ beside it", cpu_cfg == (CPU_1CORE | CPU_320) && !strcmp(CPU_N[1][cpu_get(1)], "320 MHZ"));
    cpu_cfg = CPU_1CORE | CPU_AUTO;
    cpu_put(0, 1);
    check("  2 CORES set by hand after the fallback: AFTER A CRASH forgotten", cpu_cfg == 0);

    mc_host = 0;
    cpu_cfg = CPU_1CORE | CPU_AUTO;
    check("ABOUT: CORES 1 (AFTER A CRASH) / (MENU > CORES), else the cores' own line",
          !strcmp(cpu_status(), "CORES 1 (AFTER A CRASH)") && (cpu_cfg = CPU_1CORE, !strcmp(cpu_status(), "CORES 1 (MENU > CORES)")) &&
          (cpu_cfg = 0, !strcmp(cpu_status(), "CORES 1")) && (mc_host = 1, !strcmp(cpu_status(), "CORES 2")));

    {   /* the boot after a crash beside core 1 (main.c cpu_boot, then the UI's start: cpu_off), and the second core given
         * up while it plays (the main loop: cpu_off) */
        uint32_t cfg = 0;
        memset(favorites.factory[13], 0, 32);
        saves = 0;
        cpu_cfg = (uint8_t)cfg;
        crash_log_put(CR_2C, 1, 0x0201ABCDu, 0x02012345u, 0, 1u << 21);
        cpu_cfg = (uint8_t)cpu_after(cpu_cfg, CR_2C);
        settings_save();
        k = cpu_cfg != cfg;
        if (k)
            cpu_off("2 CORES OFF AFTER A CRASH");
        check("boot after a crash with 2 CORES: logged, 1 CORE for good, saved, said with where to undo it",
              k && cpu_cfg == (CPU_1CORE | CPU_AUTO) && saves == 2 && crash_log_get(b) && !strncmp(b, "BU01 2C", 7) &&
              !strcmp(ui.msg, "2 CORES OFF AFTER A CRASH") && !strcmp(ui.msg2, "MENU > SYSTEM > CORES"));
        cpu_cfg = 0;
        cpu_off("CORE 2 FAILED: 1 CORE");
        check("the second core given up while playing: 1 CORE for good, said",
              cpu_cfg == (CPU_1CORE | CPU_AUTO) && !strcmp(ui.msg, "CORE 2 FAILED: 1 CORE"));
    }
    {   /* (0.9.19) the watchdog's timeout as the exception (DBG bit 0): WD; the 320 MHz default once */
        check("(0.9.19) DBG's watchdog bit with no EMU cause: WD (a hang with a red screen); an EMU cause still first",
              code_is(0, 1u, 0, "WD") && code_is(0, 1u | 1u << 12, 0, "WD") && code_is(2, 1u, 0, "IL"));
        check("  CPU CLOCK 320 MHZ by default: every older record moved once, 360 set by hand kept after",
              cpu_migrate(0) == (CPU_V2 | CPU_320) && cpu_migrate(CPU_1CORE | CPU_AUTO) == (CPU_V2 | CPU_320 | CPU_1CORE | CPU_AUTO) &&
              cpu_migrate(CPU_V2) == CPU_V2 && (cpu_cfg = CPU_V2 | CPU_320, cpu_put(1, 0), cpu_migrate(cpu_cfg) == CPU_V2) &&
              cpu_after(CPU_V2, CR_2C) == (CPU_V2 | CPU_1CORE | CPU_AUTO) && (cpu_cfg = CPU_V2 | CPU_1CORE | CPU_AUTO,
              cpu_put(0, 1), cpu_cfg == CPU_V2));
    }
    {   /* (0.9.19) the detail rows, the crash before, ABOUT's lines */
        crash_det_t d, g;
        char l[48], all[400];
        uint32_t i;
        memset(&favorites, 0, sizeof favorites);
        check("(0.9.19) no detail in a new record, nor in GRAIN's / TRIO's / PHYS's old stars",
              !crash_det_get(&g) && !crash_prev_get(b) && (memset(favorites.factory[8], 0xA5, 32),
              memset(favorites.factory[9], 0x5A, 32), !crash_det_get(&g) && !crash_prev_get(b)) &&
              (memset(&favorites, 0, sizeof favorites), 1));
        crash_log_put(CR_2C, 1, 0x02820A80u, 0x02034402u, 0u, 1u << 12);
        memset(&d, 0, sizeof d);
        d.core = 0; d.flags = CR_2C | CRD_AUDIO | CRD_OTHER; d.stage = 2; d.page = 5; d.sp = 0x01C79E40u; d.emu = 0;
        d.dbg = 1u << 12; d.up_ms = 754321u; d.etm0 = 0x02001234u; d.etm1 = 0x02005678u; d.clk = 0; d.mult = 270;
        d.other_pc = 0x0200F2A4u; d.bus[0] = 1; d.bus[3] = 0x100u; d.bus[4] = 0x100u; d.bus[5] = 0; d.bus[6] = 7; d.bus[7] = 9;
        crash_det_put(&d);
        crash_log_put(CR_WDT, 0xFFu, 2u, 7u, 0u, 0u);
        memset(&d, 0, sizeof d);
        d.core = 0xFFu; d.flags = CR_WDT; d.stage = 2; d.page = 7; d.mult = 240; d.clk = 1;
        crash_det_put(&d);
        check("  a second crash: the count goes on, the first one's line kept as the crash before",
              crash_count() == 2u && crash_prev_get(b) && !strcmp(b, "PC01 2C 9.9.12 02820A80 02034402") &&
              crash_log_get(b) && !strcmp(b, "WDFF 1C 9.9.12 00000002 00000007"));
        crash_det_get(&g);
        check("  its detail read back: a hang (core -), its stage and page, the clock", g.core == 0xFFu && g.stage == 2 &&
              g.page == 7 && g.mult == 240u && g.clk == 1u && !(g.flags & CRD_AUDIO));
        memset(favorites.factory[8], 0, 32);
        memset(&d, 0, sizeof d);
        d.core = 0; d.flags = CR_2C | CRD_AUDIO | CRD_OTHER; d.sp = 0x01C79E40u; d.dbg = 1u << 12; d.up_ms = 754321u;
        d.mult = 270; d.other_pc = 0x0200F2A4u; d.bus[3] = 0x100u; d.bus[4] = 0x100u;
        crash_det_put(&d);
        all[0] = 0;
        for (i = 0; i < CRASH_LINES; i++)
            if (crash_about_line(i, l)) { strcat(all, l); strcat(all, "|"); }
        check("  ABOUT's lines: count, core, time; SP and where; EMU DBG; core 2's PC; the bus masters; the boot clock",
              !strcmp(all, "CRASHES 1  CORE 0  UP 754 S|SP 01C79E40  AT AUDIO|EMU 00000000 DBG 00001000|C2 PC 0200F2A4|"
                           "BUS 00000100 00000000 00000100|CLK BOOT 0 PLL 270|"));
        for (i = 0; i < CRASH_LINES; i++)
            if (crash_about_line(i, l) && strlen(l) > 36u) break;
        check("  every line short (36 characters at most: one line of ABOUT)", i == CRASH_LINES);
        check("  the rows written: GRAIN's, TRIO's, PHYS's, SLICE's; the settings around them untouched",
              favorites.factory[7][0] == 0 && favorites.factory[10][0] == 0 && favorites.factory[5][31] == 0);
        memset(&favorites, 0, sizeof favorites);
    }
    puts(ok_all ? "cpu_safe: all passed" : "cpu_safe: FAILED");
    return !ok_all;
}
