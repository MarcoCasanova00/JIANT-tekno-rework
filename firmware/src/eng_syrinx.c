/* SPDX-License-Identifier: GPL-3.0-only */
/* SYRINX: a bird's voice organ. A voice is one bird:
 *   pressure (PRESS x velocity x the syllables) and tension (TENSE, SWEEP) drive two halves of the syrinx ->
 *   the beak (a resonant low-pass at k f0, k from BEAK) -> the trachea (fixed resonances at 1, 3, 5, 7 f1, Q 6, f1 from
 *   TRACH: the species' voice on every note) -> out; BREATH: noise in the pressure and the airflow's hiss.
 * The source: the normal form of Mindlin and Laje (Amador & Mindlin 2008) in internal time tau = gamma t,
 *   x' = y,  y' = -a - b x + x^2 - x^3 - x y - x^2 y
 * a the air sac pressure, b the labia's tension, heard through x (the labia's opening: the airflow). Near the Hopf line
 * (a = 0, b > 1/4) the cycle is small and nearly a sine: a whistle. Near the saddle-node on the invariant circle (b < 1/4)
 * its period grows and it turns into a train of pulses: a growl. PRESS walks a path from the one to the other, TENSE
 * sets how soon it dives toward the pulses; the path, its fixed point, period and level are tables (syrinx-lab
 * gen_tables.c) of the very discrete system the voices run: symplectic Euler, a step of h = 0.3 in tau.
 * Tuning. A half measures its own period P in steps (the upward crossing of the fixed point x0, placed inside the step)
 * and takes r = f P / FS steps a sample. P belongs to the discrete system at a, b, h and not to r: no loop, in tune
 * after one period at any pressure, tension, velocity or noise, for each half. Before the first period: the table's P.
 * State. A voice: s[0..5] half A (x, Y = h y, both Q29; x before the last step; the steps owed, Q16; the steps since
 * the last crossing, Q16, -1 before the first; the period, Q16 steps, 0 not yet), s[6], s[7] the beak, ph[0] the
 * syllable phase, ph[1] the noise, ph[2] the DC blocker; half B, the sweep, the gate, the second DC blocker in sy_vx. A part: its voices'
 * sum and the trachea (sy_pt): the sum of a block plays through the trachea in the next one (0.7 ms late), so the
 * trachea runs once a part, not once a voice. Integers only; products in 64 bits, no 64-bit division. */

/* ---------------------------------------------------------------- tables (syrinx-lab) --- */
/* the path over PRESS p 0..1 (17 points) x TENSE t 0..1 (9 rows): alpha Q15, the fixed point Q14, the top of the cycle
 * above it (1/64: the note-on kick's target), the period in steps Q7, 1 / the amplitude of x's fundamental Q12
 * (gen_tables.c) */
#define SY_TP 17
#define SY_TT 9
static const int16_t SY_A[9][17] = {
    {8333, 7932, 7750, 7844, 8133, 8585, 9170, 9868, 10659, 11530, 12469, 13467, 14515, 15607, 16738, 17904, 19102},
    {7081, 7001, 6630, 6286, 6478, 6833, 7322, 7924, 8619, 9395, 10237, 11138, 12089, 13083, 14117, 15184, 16283},
    {6554, 6227, 6073, 5687, 5286, 5184, 5576, 6081, 6680, 7359, 8106, 8911, 9765, 10663, 11600, 12570, 13571},
    {6554, 6554, 6279, 5549, 5089, 4621, 4313, 4351, 4853, 5435, 6086, 6795, 7553, 8355, 9196, 10070, 10974},
    {6554, 6554, 6554, 6554, 6037, 4934, 4302, 3822, 3595, 3651, 4190, 4801, 5464, 6170, 6914, 7692, 8500},
    {6554, 6554, 6554, 6554, 6554, 6554, 6130, 4671, 3777, 3244, 3027, 3109, 3513, 4122, 4769, 5451, 6163},
    {6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 5082, 3607, 2896, 2573, 2585, 2855, 3365, 3980},
    {6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 4435, 2959, 2329, 2130, 2251},
    {6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 6554, 4574, 2727},
};
static const int16_t SY_X0[9][17] = {
    {-6202, -6256, -6399, -6652, -6980, -7365, -7788, -8236, -8698, -9168, -9638, -10107, -10571, -11029, -11479, -11922, -12356},
    {-5394, -5551, -5591, -5646, -5939, -6301, -6712, -7156, -7619, -8092, -8569, -9044, -9515, -9980, -10436, -10885, -11324},
    {-4869, -4897, -5005, -5011, -5010, -5159, -5552, -5988, -6452, -6931, -7416, -7900, -8381, -8855, -9321, -9778, -10225},
    {-4586, -4740, -4778, -4604, -4549, -4484, -4501, -4714, -5176, -5661, -6156, -6654, -7149, -7637, -8116, -8585, -9044},
    {-4321, -4457, -4599, -4745, -4665, -4287, -4111, -4002, -4040, -4257, -4758, -5274, -5789, -6297, -6796, -7283, -7759},
    {-4074, -4194, -4318, -4447, -4580, -4717, -4671, -4104, -3752, -3575, -3593, -3817, -4254, -4792, -5321, -5836, -6337},
    {-3845, -3950, -4058, -4170, -4286, -4406, -4530, -4658, -4790, -4237, -3560, -3240, -3155, -3318, -3673, -4181, -4723},
    {-3632, -3723, -3817, -3915, -4015, -4119, -4227, -4337, -4451, -4569, -4690, -4815, -3915, -3151, -2824, -2801, -3042},
    {-3436, -3514, -3596, -3679, -3766, -3855, -3947, -4042, -4140, -4241, -4345, -4451, -4561, -4674, -4790, -3957, -2949},
};
static const uint8_t SY_XD[9][17] = {
    {65, 66, 67, 69, 72, 75, 78, 81, 84, 87, 89, 91, 93, 95, 96, 97, 97},
    {60, 61, 61, 62, 65, 68, 71, 75, 78, 81, 84, 87, 89, 91, 93, 95, 96},
    {56, 56, 57, 58, 58, 60, 63, 67, 70, 74, 77, 80, 83, 86, 88, 91, 93},
    {54, 55, 55, 54, 54, 54, 55, 57, 61, 65, 69, 73, 76, 79, 82, 85, 87},
    {52, 53, 54, 55, 55, 52, 51, 50, 51, 54, 58, 63, 67, 71, 74, 78, 80},
    {51, 51, 52, 53, 54, 55, 55, 51, 48, 47, 47, 50, 55, 60, 64, 68, 72},
    {49, 50, 50, 51, 52, 53, 54, 54, 55, 51, 46, 44, 43, 45, 49, 55, 60},
    {47, 48, 49, 49, 50, 51, 52, 52, 53, 54, 55, 56, 49, 43, 40, 40, 43},
    {46, 47, 47, 48, 49, 49, 50, 50, 51, 52, 52, 53, 54, 55, 55, 49, 41},
};
static const int16_t SY_P[9][17] = {
    {3468, 3753, 4082, 4434, 4830, 5273, 5768, 6312, 6900, 7508, 8095, 8583, 8838, 8669, 7925, 6774, 5599},
    {3446, 3630, 3947, 4400, 4789, 5234, 5743, 6325, 6977, 7702, 8482, 9294, 10080, 10738, 11074, 10787, 9644},
    {3309, 3501, 3700, 4026, 4513, 5121, 5616, 6195, 6872, 7652, 8531, 9508, 10567, 11679, 12786, 13766, 14373},
    {3124, 3221, 3376, 3660, 3975, 4430, 5079, 5912, 6562, 7344, 8261, 9320, 10515, 11842, 13293, 14841, 16441},
    {2974, 3049, 3132, 3225, 3415, 3793, 4198, 4749, 5513, 6673, 7578, 8649, 9914, 11366, 12999, 14808, 16788},
    {2848, 2908, 2973, 3043, 3122, 3206, 3372, 3805, 4292, 4886, 5679, 6866, 8448, 9928, 11703, 13733, 15982},
    {2739, 2788, 2839, 2895, 2956, 3021, 3091, 3168, 3254, 3626, 4198, 4791, 5521, 6530, 8226, 10572, 13082},
    {2644, 2684, 2727, 2771, 2819, 2870, 2924, 2982, 3046, 3114, 3189, 3272, 3792, 4452, 5105, 5872, 6953},
    {2559, 2594, 2628, 2665, 2703, 2744, 2787, 2832, 2881, 2932, 2987, 3046, 3110, 3179, 3254, 3722, 4457},
};
static const int16_t SY_G[9][17] = {
    {6220, 6261, 6190, 6314, 6360, 6583, 6788, 6906, 7009, 7361, 7779, 8093, 8194, 7853, 7342, 6634, 5998},
    {6342, 6517, 6503, 6802, 6822, 6753, 6866, 7159, 7365, 7759, 8110, 8448, 8818, 9132, 9416, 9240, 8565},
    {6577, 6812, 6685, 6969, 7244, 7494, 7274, 7476, 7591, 7862, 8277, 8827, 9310, 10051, 10459, 11099, 11539},
    {6781, 6812, 6815, 7011, 7361, 7465, 7728, 8075, 8098, 8243, 8500, 8897, 9663, 10250, 10959, 11791, 12868},
    {6911, 6682, 6750, 6808, 6815, 7299, 7604, 8173, 8481, 8805, 8866, 9068, 9619, 10214, 11107, 12077, 13382},
    {6953, 6785, 6872, 6698, 6769, 6850, 6887, 7424, 8061, 8665, 8952, 9175, 9744, 10064, 10854, 11919, 12950},
    {7025, 6849, 6989, 6836, 6964, 6768, 6861, 6655, 6727, 7341, 8141, 8956, 9523, 9780, 10114, 10841, 12042},
    {7023, 6872, 7011, 6912, 7006, 6919, 6742, 6866, 6692, 6811, 6595, 6671, 7611, 8790, 9629, 10059, 10305},
    {6981, 7122, 7062, 6941, 7108, 6985, 6855, 7035, 6871, 6715, 6847, 6692, 6796, 6622, 6727, 7701, 9076},
};
/* the beak's gain at f0 undone, Q12, BEAK 0..128 by 4 */
static const uint16_t SY_BEAKG[33] = {
    4118, 3486, 3033, 2746, 2605, 2576, 2624, 2717, 2832, 2955, 3077,
    3192, 3299, 3396, 3483, 3560, 3629, 3689, 3742, 3788, 3829, 3864,
    3895, 3922, 3945, 3965, 3982, 3998, 4011, 4022, 4032, 4041, 4048};

/* ---------------------------------------------------------------- the state --- */
#define SY_POLY 4                  /* voices a part (engine_t.poly) */
#define SY_H 1288490189            /* h = 0.3, Q32 */
#define SY_H2 386547057            /* h^2 = 0.09, Q32 */
#define SY_RMAX (8 << 16)          /* steps a sample at most: above, PRESS steps back (the pulses would alias) */
#define SY_LEV 14746               /* the source's fundamental: 0.45 of full scale */
#define SY_IMAX 1932735283u        /* 0.45 FS as a phase increment: the filters' highest frequency */
typedef struct {
    int32_t x, Y, xp, acc, since, P;
} sy_half_t;
typedef struct {
    sy_half_t B;                   /* the second half (DUET) */
    int32_t sw;                    /* the sweep's offset, 1/256 semitone (decays to 0) */
    int32_t gate;                  /* the syllable gate at the block's start, Q15 */
    int32_t dc2;                   /* the DC blocker after the soft knee (a pulse's peak clipped leaves DC), Q8 */
} sy_vx_t;
typedef struct {
    int32_t buf[CTL];              /* the voices' sum of the block (plays through the trachea in the next one) */
    int32_t z1[4], z2[4];          /* the trachea's stages */
    int32_t c1[4], c2[4], c3[4];   /* their coefficients, Q16 */
    int32_t tkey;                  /* the TRACH they are for (-1: none yet) */
    uint32_t blk, bufblk;          /* blocks so far, the block buf holds */
    uint8_t ns, fresh;             /* stages below 0.45 FS; the block's first render still to come */
} sy_part_t;
static sy_vx_t sy_vx[NPART][SY_POLY];
static sy_part_t sy_pt[NPART];

static sy_part_t *sy_part(const track_t *t) { return &sy_pt[(uint32_t)(t - trk) % NPART]; }
static sy_vx_t *sy_vxp(const track_t *t, const voice_t *v)
{
    return &sy_vx[(uint32_t)(t - trk) % NPART][(uint32_t)(v - t->v) & (SY_POLY - 1u)];
}
static inline int32_t sy_mq(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 32); }
/* pitch_inc of 1/16 semitone p16, clamped (one copy: the block's four) */
static __attribute__((noinline)) uint32_t sy_inc(int32_t p16) { return pitch_inc((uint32_t)clamp(p16, 0, 2047)); }
static inline int32_t sy_m16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

/* ---------------------------------------------------------------- the path --- */
/* a point of the tables at p, t (Q15): the cell and the fractions */
typedef struct { uint32_t i, j; int32_t fp, ft; } sy_at_t;
static void sy_at(sy_at_t *q, int32_t p, int32_t t)
{
    int32_t a = clamp(p, 0, 32767) * (SY_TP - 1), b = clamp(t, 0, 32767) * (SY_TT - 1);
    q->i = (uint32_t)a >> 15;
    q->j = (uint32_t)b >> 15;
    q->fp = a & 32767;
    q->ft = b & 32767;
}
/* a table (SY_TT x SY_TP) at q, bilinear */
static __attribute__((noinline)) int32_t sy_lut(const int16_t (*tab)[SY_TP], const sy_at_t *q)
{
    const int16_t *r = &tab[q->j][q->i];
    int32_t u = r[0] + (((r[1] - r[0]) * q->fp) >> 15), w = r[SY_TP] + (((r[SY_TP + 1] - r[SY_TP]) * q->fp) >> 15);
    return u + (((w - u) * q->ft) >> 15);
}
/* the tension on the path, Q15: b = 0.15 + 0.55 t + (0.25 t - 0.70) p (TENSE 0: 0.15 .. -0.55, 1: 0.70 .. 0.25) */
static int32_t sy_beta(int32_t p, int32_t t)
{
    p = clamp(p, 0, 32767);
    t = clamp(t, 0, 32767);
    return 4915 + ((t * 18022) >> 15) + (((-22938 + (t >> 2)) * p) >> 15);
}
/* PRESS with the velocity (soft notes nearer the whistle), Q15 */
static int32_t sy_press(const int16_t *p, uint32_t vel)
{
    return (clamp(p[P_E0], 0, 127) * 258 * (18022 + (int32_t)(vel > 127u ? 127u : vel) * 116)) >> 15;
}

/* an SVF's coefficients (Q16) at the frequency of phase increment inc, damping kd (1 / Q, Q16); g = tan(pi f / FS) as
 * sin / cos of the sine table (cos(0.45 pi) > 0.15) */
static void sy_svf(uint32_t inc, int32_t kd, int32_t *c1, int32_t *c2, int32_t *c3)
{
    int32_t g, den;
    inc = (inc > SY_IMAX ? SY_IMAX : inc) >> 1;
    g = (int32_t)(((uint32_t)sine_i(inc) << 15) / (uint32_t)sine_i(inc + 0x40000000u)) * 2;
    den = 65536 + (int32_t)(((int64_t)g * (g + kd)) >> 16);
    *c1 = (int32_t)(0x7FFFFFFFu / (uint32_t)den) << 1;
    *c2 = sy_m16(*c1, g);
    *c3 = sy_m16(*c2, g);
}
/* the trachea at TRACH k: f1 = 6 kHz (0) .. 200 Hz (127), its odd multiples below 0.45 FS */
static void sy_trachea(sy_part_t *P, int32_t k)
{
    uint32_t inc1 = sy_inc(1830 - (clamp(k, 0, 127) * 944) / 127), s;
    P->tkey = k;
    P->ns = 0;
    for (s = 0; s < 4u; s++) {
        uint64_t inc = (uint64_t)inc1 * (2u * s + 1u);
        if (inc > SY_IMAX)
            break;
        sy_svf((uint32_t)inc, 10923, &P->c1[s], &P->c2[s], &P->c3[s]);
        P->ns = (uint8_t)(s + 1u);
    }
}
/* the last block's sum through the trachea into out (the block's first render), then the sum starts again */
static void sy_flush(sy_part_t *P, int32_t *out, uint32_t n)
{
    uint32_t i, s, ns = P->ns;
    P->fresh = 0;
    if (P->bufblk + 1u == P->blk) {
        for (i = 0; i < n; i++) {
            int32_t x = P->buf[i];
            for (s = 0; s < ns; s++) {
                int32_t z1 = P->z1[s], z2 = P->z2[s], v3 = x - z2, v1, v2;
                v1 = (int32_t)(((int64_t)P->c1[s] * z1 + (int64_t)P->c2[s] * v3) >> 16);
                v2 = z2 + (int32_t)(((int64_t)P->c2[s] * z1 + (int64_t)P->c3[s] * v3) >> 16);
                P->z1[s] = 2 * v1 - z1;
                P->z2[s] = 2 * v2 - z2;
                x += sy_m16(v1, 10923);              /* the band-pass at its peak 1: +6 dB at each resonance */
            }
            out[i] += sy_m16(x, 39322);              /* x 0.6 */
        }
    } else {                                         /* nothing last block: the trachea from rest */
        for (s = 0; s < 4u; s++)
            P->z1[s] = P->z2[s] = 0;
    }
    for (i = 0; i < CTL; i++)
        P->buf[i] = 0;
    P->bufblk = P->blk;
}

/* ---------------------------------------------------------------- a half of the syrinx --- */
/* one step of h: Y += h^2 (-a - b x + x^2 - x^3) - h (x + x^2) Y, x += Y (Y = h y; ah = h^2 a, bh = h^2 b, Q29);
 * the upward crossing of x0 inside the step measures the period (accepted between pmin and pmax, Q16 steps) */
static inline void sy_step(sy_half_t *H, int32_t ah, int32_t bh, int32_t x0, int32_t pmin, int32_t pmax)
{
    int32_t x = H->x, Y = H->Y, x2 = sy_mq(x, x) << 3, xn;
    Y += sy_mq(x, sy_mq(SY_H2, x - x2) - bh) * 8 - sy_mq(sy_mq(SY_H, x + x2), Y) * 8 - ah;
    Y = clamp(Y, -(1 << 29), 1 << 29);
    xn = clamp(x + Y, -805306368, 805306368);        /* |x| <= 1.5 (it stays below 1.4): x - x^2, x + x^2 within +-4 */
    if (x < x0 && xn >= x0) {
        uint32_t num = (uint32_t)(x0 - x) >> 14, den = (uint32_t)(xn - x) >> 14;
        int32_t fr = den ? (int32_t)((num << 16) / den) : 0, pm;
        fr = fr > 65535 ? 65535 : fr;
        pm = H->since + fr;
        if (H->since >= 0 && pm > pmin && pm < pmax)
            H->P = H->P ? H->P + ((pm - H->P) >> 1) : pm;
        H->since = 65536 - fr;
    } else if (H->since >= 0 && H->since < (1 << 30)) {
        H->since += 65536;
    }
    H->xp = x;
    H->x = xn;
    H->Y = Y;
}
static void sy_half_start(sy_half_t *H, int32_t x)
{
    H->x = H->xp = x;
    H->Y = 0;
    H->acc = 0;
    H->since = -1;
    H->P = 0;
}

/* ---------------------------------------------------------------- the engine --- */
/* the sweep's offset times dk (Q16), toward 0 and at least a unit a block: it lands on the key from either side */
static int32_t sy_decay(int32_t sw, int32_t dk)
{
    int32_t a = sw < 0 ? -sw : sw, d = (int32_t)(((int64_t)a * (65536 - dk)) >> 16) + 1;
    a = a > d ? a - d : 0;
    return sw < 0 ? -a : a;
}
static void syrinx_block(track_t *t)
{
    sy_part_t *P = sy_part(t);
    P->blk++;
    P->fresh = 1;
    if (P->tkey != t->p[P_E5] || !P->ns)
        sy_trachea(P, t->p[P_E5]);
}

static void syrinx_note_on(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    sy_vx_t *X = sy_vxp(t, v);
    X->sw = -clamp(p[P_E2], -64, 63) * 48;          /* the chirp: from 12 semitones above (SWEEP -64) or below (+63) */
    if (!v->env && !v->env_out) {                    /* a fresh voice: the labia kicked toward the cycle's top */
        sy_at_t q;
        int32_t x0, xd, k;
        sy_at(&q, sy_press(p, v->vel), clamp(p[P_E1], 0, 127) * 258);
        x0 = sy_lut(SY_X0, &q) * 32768;
        xd = SY_XD[q.j + (q.ft >> 14)][q.i + (q.fp >> 14)] << 23;   /* (the nearest point: a starting place) */
        k = 13107 + (int32_t)(v->vel > 127u ? 127u : v->vel) * 129;   /* 0.4 .. 0.9 of the way, by velocity */
        sy_half_start(&X->B, x0 + sy_m16(xd, (k * 29491) >> 14));       /* (B 0.9 of A's) */
        v->s[0] = v->s[2] = x0 + sy_m16(xd, k * 2);
        v->s[1] = v->s[3] = v->s[5] = 0;
        v->s[4] = -1;
        v->s[6] = v->s[7] = 0;
        v->ph[0] = 0;                                /* the syllable from its start */
        v->ph[2] = 0;
        X->dc2 = 0;
        if (!v->ph[1])
            v->ph[1] = 0x2545F491u + v->note * 2654435761u;
        X->gate = 0;
    }
}

static void syrinx_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    sy_part_t *P = sy_part(t);
    sy_vx_t *X = sy_vxp(t, v);
    sy_half_t H[2];
    sy_at_t q;
    int32_t pr = clamp(sy_press(p, v->vel) + (m->shape - (64 << 8)) * 2, 0, 32767), tn = clamp(p[P_E1], 0, 127) * 258;
    int32_t ah, bh, x0, pt, gs, sig, hs, g0, g1, sw = X->sw, z1 = v->s[6], z2 = v->s[7], dc = (int32_t)v->ph[2], d2 = X->dc2;
    int32_t c1, c2, c3, duet = clamp(p[P_E6], 0, 127), breath = clamp(p[P_E7], 0, 127), pmin, pmax, r[2], s, v1, v2, v3, y;
    uint32_t i, k, nh = duet ? 2u : 1u, incA, incB = 0, syl = (uint32_t)clamp(p[P_E3], 0, 127), sph = v->ph[0];
    int32_t rng = (int32_t)v->ph[1];
    if (P->fresh)
        sy_flush(P, out, n);
    H[0].x = v->s[0]; H[0].Y = v->s[1]; H[0].xp = v->s[2]; H[0].acc = v->s[3]; H[0].since = v->s[4]; H[0].P = v->s[5];
    H[1] = X->B;
    /* the syllables: the gate (sin^2 over 70 % of each), the pressure with it, a new chirp each */
    g0 = X->gate;
    g1 = 32767;
    if (syl) {
        uint32_t inc = sy_inc((int32_t)(190u + ((syl - 1u) * 639u) / 126u)) >> 2, s0 = sph, dk;   /* 4 .. 40 Hz */
        sph += inc * n;
        if (sph < s0)                                /* a new syllable */
            sw = -clamp(p[P_E2], -64, 63) * 48;
        g1 = sph < 3006477107u ? (32767 - sine_i((uint32_t)(((uint64_t)sph * 1533916891u) >> 30) + 0x40000000u)) >> 1 : 0;
        pr = (pr * (19661 + ((g1 * 13107) >> 15))) >> 15;
        dk = 65536u - (((inc >> 10) * 5u) >> 2);     /* the chirp: 40 % of a syllable */
        sw = sy_decay(sw, (int32_t)dk);
    } else {
        sw = sy_decay(sw, 64748);                    /* ~60 ms */
    }
    if (!syl && !g0)
        g0 = 32767;
    X->gate = g1;
    /* the pitches: the key, the sweep, half B's detune (DUET 1..63: up to 80 cents; 64..127: 1..12 semitones) */
    {
        int32_t o16 = sw >> 4, fadd = ((sw & 15) * 947) >> 10;
        incA = sw ? sy_inc(m->pitch16 + o16) : m->inc;
        if (sw)
            incA += (uint32_t)((int32_t)(incA >> 12) * (m->fine + fadd));
        if (duet && duet < 64) {
            incB = incA + (incA >> 12) * (uint32_t)(1 + (duet * duet * 189) / 3969);
        } else if (duet) {
            incB = sy_inc(m->pitch16 + o16 + 16 * (1 + ((duet - 64) * 11 + 31) / 63));
            incB += (uint32_t)((int32_t)(incB >> 12) * (m->fine + fadd));
        }
        /* the beak: a resonant low-pass at k f0, k 0.8 .. 8 (BEAK, FLT) */
        {
            int32_t b = clamp(p[P_E4] + (m->cutoff >> 8), 0, 127), j = b >> 2;
            sy_svf(sy_inc(m->pitch16 + o16 - 62 + (b * 638) / 127), 43691, &c1, &c2, &c3);
            gs = SY_BEAKG[j] + (((SY_BEAKG[j + 1] - SY_BEAKG[j]) * (b & 3)) >> 2);
        }
    }
    /* the path; above SY_RMAX steps a sample PRESS steps back (a shorter period) */
    pr = clamp(pr, 0, 32767);
    sy_at(&q, pr, tn);
    pt = sy_lut(SY_P, &q) << 9;
    {
        uint32_t inc = incA > incB ? incA : incB;
        while (pr > 0 && (int32_t)(((uint64_t)inc * (uint32_t)pt) >> 32) > SY_RMAX) {
            pr = pr > 2048 ? pr - 2048 : 0;
            sy_at(&q, pr, tn);
            pt = sy_lut(SY_P, &q) << 9;
        }
    }
    ah = sy_mq(SY_H2, sy_lut(SY_A, &q) * 16384);
    bh = sy_mq(SY_H2, sy_beta(pr, tn) * 16384);
    x0 = sy_lut(SY_X0, &q) * 32768;
    gs = (int32_t)(((int64_t)sy_lut(SY_G, &q) * SY_LEV * gs) >> 21);   /* the level: the fundamental at SY_LEV */
    if (duet)
        gs = sy_m16(gs, 39322);                      /* two halves: 0.6 each */
    sig = sy_mq(SY_H2, (breath * (9830 + pr) * 165) >> 5);   /* h^2 0.04 (0.3 + p) BREATH: noise in the pressure, Q29 */
    hs = (breath * (6554 + pr) * 58) >> 15;                   /* 0.5 (0.2 + p) BREATH: the hiss, Q15 */
    pmin = pt >> 2;
    pmax = pt < (1 << 28) ? pt << 2 : 1 << 30;
    for (k = 0; k < nh; k++) {
        r[k] = (int32_t)(((uint64_t)(k ? incB : incA) * (uint32_t)(H[k].P ? H[k].P : pt)) >> 32);
        r[k] = r[k] > SY_RMAX ? SY_RMAX : r[k];
    }
    for (i = 0; i < n; i++) {
        int32_t an = ah;
        if (breath) {
            uint32_t r = noise32(&rng);
            an += (((int32_t)r >> 16) * (sig >> 7)) >> 8;
            s = (((int32_t)(int16_t)(r & 0xFFFFu)) * hs) >> 15;
        } else {
            s = 0;
        }
        for (k = 0; k < nh; k++) {                    /* the halves (one copy of the step: flash is scarce) */
            sy_half_t *Q = &H[k];
            for (Q->acc += r[k]; Q->acc >= 65536; Q->acc -= 65536)
                sy_step(Q, an, bh, x0, pmin, pmax);
            s += sy_mq(Q->xp + sy_m16(Q->x - Q->xp, Q->acc) - x0, gs);   /* (x - x0, Q29, x gs / 8: Q15) */
        }
        dc += (s * 256 - dc) >> 9;                   /* DC blocker, ~14 Hz */
        s -= dc >> 8;
        v3 = s - z2;                                 /* the beak */
        v1 = (int32_t)(((int64_t)c1 * z1 + (int64_t)c2 * v3) >> 16);
        v2 = z2 + (int32_t)(((int64_t)c2 * z1 + (int64_t)c3 * v3) >> 16);
        z1 = 2 * v1 - z1;
        z2 = 2 * v2 - z2;
        y = soft_knee(v2, 22000);
        d2 += (y * 256 - d2) >> 9;
        y = clamp(y - (d2 >> 8), -32767, 32767);
        if (syl)
            y = (y * (g0 + (((g1 - g0) * (int32_t)i) >> CTL_LOG2))) >> 15;
        P->buf[i] += voice_amp(y, m, i);
    }
    v->s[0] = H[0].x; v->s[1] = H[0].Y; v->s[2] = H[0].xp; v->s[3] = H[0].acc; v->s[4] = H[0].since; v->s[5] = H[0].P;
    v->s[6] = z1;
    v->s[7] = z2;
    X->B = H[1];
    X->sw = sw;
    X->dc2 = d2;
    v->ph[0] = sph;
    v->ph[1] = (uint32_t)rng;
    v->ph[2] = (uint32_t)dc;
}

/* {PRESS, TENSE, SWEEP, SYLL, BEAK, TRACH, DUET, BREATH} */
static const preset_t SYRINX_PRESETS[] = {
    /* whistlers: the Hopf side (TENSE high), syllables and chirps */
    {"WHISTLE", {20, 115, 0, 0, 40, 38, 0, 6}, {5, 60, 100, 45}, 0, 0, FX(0, 10, 20, 35), PAT(4)},
    {"CANARY", {45, 100, -20, 70, 60, 20, 0, 6}, {0, 60, 110, 40}, 0, 0, FX(0, 0, 25, 40), PAT(7)},
    {"NIGHTSONG", {50, 85, 30, 35, 70, 30, 8, 8}, {2, 70, 105, 50}, 0, 0, FX(0, 10, 30, 50), PAT(3)},
    {"THRUSH", {35, 95, 20, 0, 55, 35, 0, 6}, {0, 50, 90, 45}, 0, 0, FX(0, 0, 30, 40), PAT(3)},
    {"SPARROW", {50, 75, -60, 45, 80, 15, 0, 10}, {0, 40, 90, 35}, 0, 0, FX(0, 0, 20, 30), PAT(7)},
    {"SKYLARK", {60, 70, 25, 100, 75, 25, 0, 8}, {3, 60, 110, 45}, 0, 0, FX(0, 10, 35, 45), PAT(4)},
    /* the pulses: TENSE low, PRESS high (a harmonic stack, a caw, a quack) */
    {"FINCH", {105, 40, -8, 0, 100, 55, 0, 12}, {0, 30, 60, 30}, 0, 0, FX(0, 0, 20, 25), PAT(6)},
    {"CROW", {127, 10, -10, 0, 110, 95, 6, 40}, {2, 50, 90, 40}, 0, 0, FX(10, 0, 15, 30), PAT(2)},
    {"QUACK", {127, 0, -15, 0, 75, 70, 0, 25}, {0, 35, 70, 30}, 0, 0, FX(0, 0, 10, 20), PAT(6)},
    {"SCREECH", {120, 25, 10, 0, 127, 10, 12, 50}, {0, 45, 80, 35}, 0, 0, FX(15, 0, 20, 25), PAT(4)},
    /* hoots: a closed beak, a long trachea */
    {"DOVE", {8, 110, 25, 0, 20, 110, 0, 14}, {20, 80, 90, 55}, 0, 0, FX(0, 0, 20, 45), PAT(5)},
    {"OWL", {15, 120, 5, 0, 10, 120, 0, 30}, {30, 100, 100, 60}, 0, 0, FX(0, 0, 25, 55), PAT(5)},
    {"CUCKOO", {15, 105, -5, 0, 30, 85, 0, 8}, {4, 60, 90, 45}, 0, 0, FX(0, 0, 30, 45), PAT(7)},
    /* two halves: an interval (the crane's fifth, a thrush's two voices), beats */
    {"CRANE", {95, 30, -6, 0, 90, 127, 100, 20}, {8, 80, 100, 50}, 0, 0, FX(0, 10, 20, 50), PAT(5)},
    {"TWO VOICE", {40, 80, 15, 0, 60, 30, 87, 6}, {4, 70, 100, 50}, 0, 0, FX(0, 0, 30, 50), PAT(3)},
    {"SHIMMER", {30, 90, 0, 0, 50, 40, 20, 4}, {40, 90, 110, 70}, 0, 0, FX(0, 30, 30, 60), PAT(5)},
    {"GROWL LEAD", {100, 35, 0, 0, 90, 60, 4, 10}, {0, 70, 100, 40}, 0, 1, FX(10, 10, 30, 30), PAT(4)},
};

static const engine_t ENG_SYRINX = {
    .name = "SYRINX",
    .page_title = {"SYRINX", "BIRD"},
    .edit = {
        {"PRESS", F_PCT, 0, 127, 40, 0, 0},
        {"TENSE", F_PCT, 0, 127, 80, 0, 0},
        {"SWEEP", F_BIPCT, -64, 63, 0, 0, 0},
        {"SYLL", F_PCT, 0, 127, 0, 0, 0},
        {"BEAK", F_PCT, 0, 127, 64, 0, 0},
        {"TRACH", F_PCT, 0, 127, 50, 0, 0},
        {"DUET", F_PCT, 0, 127, 0, 0, 0},
        {"BREATH", F_PCT, 0, 127, 8, 0, 0},
    },
    .presets = SYRINX_PRESETS,
    .npresets = NELEM(SYRINX_PRESETS),
    .note_on = syrinx_note_on,
    .render = syrinx_render,
    .knob = {P_E0, P_E1, P_E4, P_E5},
    .poly = SY_POLY,
    .keep = 0,                   /* (note_on keeps a sounding voice's state itself) */
    .block = syrinx_block,
};
