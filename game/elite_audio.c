/*
 * ThumbyElite — procedural audio.
 *
 * 6 one-shot voices (square/saw/sine/noise, linear freq sweep, attack/
 * decay envelope) + 1 continuous engine-hum voice. int32 mix, hard
 * clamp. Phase accumulators in Q16; noise from a 16-bit LFSR.
 */
#include "elite_audio.h"
#include "elite_weapons.h"
#include <string.h>

#define N_VOICES 6

typedef enum { W_SQUARE = 0, W_SAW, W_SINE, W_NOISE } Wave;

typedef struct {
    uint8_t active, wave;
    float freq0, freq1;      /* Hz sweep over the duration */
    float amp;
    float t, dur;            /* seconds */
    float attack;            /* seconds */
    uint32_t phase;          /* Q16 cycles */
} Voice;

static Voice s_v[N_VOICES];
static uint16_t s_lfsr = 0xACE1u;

/* Engine hum. */
static float s_eng_freq = 40.0f, s_eng_amp = 0.0f;
static float s_eng_freq_t = 40.0f, s_eng_amp_t = 0.0f;
static uint32_t s_eng_phase;

void audio_init(void) {
    memset(s_v, 0, sizeof s_v);
    s_eng_amp = s_eng_amp_t = 0;
}

static Voice *voice(void) {
    /* Free voice, else the quietest/oldest. */
    for (int i = 0; i < N_VOICES; i++)
        if (!s_v[i].active) return &s_v[i];
    Voice *best = &s_v[0];
    for (int i = 1; i < N_VOICES; i++)
        if (s_v[i].t / s_v[i].dur > best->t / best->dur) best = &s_v[i];
    return best;
}

static void play(Wave w, float f0, float f1, float amp, float attack,
                 float dur) {
    Voice *v = voice();
    v->active = 1;
    v->wave = (uint8_t)w;
    v->freq0 = f0;
    v->freq1 = f1;
    v->amp = amp > 1 ? 1 : amp;
    v->attack = attack;
    v->t = 0;
    v->dur = dur;
    v->phase = 0;
}

/* --- SFX recipes -------------------------------------------------------*/
void sfx_weapon(int t, float amp) {
    if (amp < 0.05f) return;
    switch (t) {
    case WPN_PULSE_S: play(W_SQUARE, 1700, 500, 0.30f * amp, 0.002f, 0.07f); break;
    case WPN_PULSE_M: play(W_SQUARE, 1300, 350, 0.36f * amp, 0.002f, 0.09f); break;
    case WPN_PULSE_L: play(W_SQUARE, 950, 220, 0.42f * amp, 0.003f, 0.13f); break;
    case WPN_BEAM:    play(W_SAW, 2100, 1900, 0.16f * amp, 0.001f, 0.06f); break;
    case WPN_PHOTON:  play(W_SINE, 300, 900, 0.40f * amp, 0.010f, 0.18f); break;
    case WPN_GAUSS:
        play(W_NOISE, 4000, 800, 0.35f * amp, 0.001f, 0.10f);
        play(W_SINE, 180, 60, 0.30f * amp, 0.002f, 0.12f);
        break;
    case WPN_AUTOCANNON: play(W_NOISE, 3000, 1500, 0.22f * amp, 0.001f, 0.045f); break;
    default:          /* missiles: launch whoosh */
        play(W_NOISE, 900, 2400, 0.30f * amp, 0.030f, 0.25f);
        break;
    }
}

void sfx_explosion(float amp, float big01) {
    if (amp < 0.05f) return;
    float dur = 0.45f + 0.55f * big01;
    play(W_NOISE, 2600, 200, 0.55f * amp, 0.004f, dur);
    play(W_SINE, 110, 35, 0.50f * amp, 0.005f, dur * 1.2f);
}

void sfx_hit_shield(void) { play(W_SINE, 1400, 900, 0.30f, 0.001f, 0.06f); }
void sfx_hit_hull(void) {
    play(W_NOISE, 1800, 400, 0.40f, 0.001f, 0.09f);
    play(W_SINE, 140, 70, 0.35f, 0.002f, 0.10f);
}
void sfx_ui_move(void)   { play(W_SQUARE, 880, 880, 0.10f, 0.001f, 0.025f); }
void sfx_ui_select(void) { play(W_SQUARE, 1320, 1760, 0.14f, 0.001f, 0.05f); }
void sfx_ui_deny(void)   { play(W_SQUARE, 240, 180, 0.16f, 0.001f, 0.09f); }
void sfx_scoop(void)     { play(W_SINE, 620, 1240, 0.22f, 0.004f, 0.14f); }
void sfx_jump(void) {
    play(W_NOISE, 300, 3600, 0.30f, 0.40f, 1.8f);
    play(W_SAW, 60, 480, 0.22f, 0.50f, 2.2f);
}
void sfx_dock(void) {
    play(W_NOISE, 700, 250, 0.25f, 0.002f, 0.10f);
    play(W_SINE, 90, 60, 0.40f, 0.002f, 0.16f);
}
void sfx_klaxon(void) {
    play(W_SQUARE, 700, 700, 0.22f, 0.005f, 0.12f);
    play(W_SQUARE, 520, 520, 0.22f, 0.005f, 0.12f);
}

void audio_engine_set(float throttle01, float speed01) {
    s_eng_freq_t = 36.0f + 70.0f * throttle01;
    s_eng_amp_t = 0.05f + 0.13f * speed01;
}

/* --- mixing ------------------------------------------------------------*/
static inline int16_t wave_sample(uint8_t w, uint32_t phase) {
    uint16_t p = (uint16_t)(phase >> 1);     /* cycle in Q15 */
    switch (w) {
    case W_SQUARE: return (p & 0x8000) ? 12000 : -12000;
    case W_SAW:    return (int16_t)((int32_t)p - 16384) / 2 * 3 / 2;
    case W_SINE: {
        /* Parabolic sine approximation. */
        int32_t x = (int32_t)p - 16384;       /* -16384..16383 */
        int32_t up = (x < 0) ? -1 : 1;
        int32_t ax = x < 0 ? -x : x;
        int32_t y = (ax * (32768 - ax)) >> 13;
        return (int16_t)(up * y * 12000 / 32768 * 2);
    }
    default: {
        s_lfsr = (uint16_t)((s_lfsr >> 1) ^ (-(int)(s_lfsr & 1) & 0xB400u));
        return (int16_t)((int32_t)s_lfsr - 32768) / 3;
    }
    }
}

int audio_render(int16_t *out, int n) {
    const float dt = 1.0f / (float)ELITE_AUDIO_RATE;
    for (int s = 0; s < n; s++) {
        int32_t mix = 0;

        /* Engine hum: chase targets, triangle-ish via saw fold. */
        s_eng_freq += (s_eng_freq_t - s_eng_freq) * 0.0004f;
        s_eng_amp += (s_eng_amp_t - s_eng_amp) * 0.0004f;
        if (s_eng_amp > 0.005f) {
            s_eng_phase += (uint32_t)(s_eng_freq * 65536.0f * dt);
            int16_t v = wave_sample(W_SAW, s_eng_phase);
            /* Soften: add a sub-octave square. */
            int16_t v2 = wave_sample(W_SQUARE, s_eng_phase >> 1);
            mix += (int32_t)((v / 2 + v2 / 3) * s_eng_amp);
        }

        for (int i = 0; i < N_VOICES; i++) {
            Voice *v = &s_v[i];
            if (!v->active) continue;
            v->t += dt;
            if (v->t >= v->dur) { v->active = 0; continue; }
            float k = v->t / v->dur;
            float freq = v->freq0 + (v->freq1 - v->freq0) * k;
            v->phase += (uint32_t)(freq * 65536.0f * dt);
            float env = (v->t < v->attack)
                            ? v->t / v->attack
                            : 1.0f - (v->t - v->attack) /
                                  (v->dur - v->attack + 1e-6f);
            mix += (int32_t)((float)wave_sample(v->wave, v->phase) *
                             v->amp * env);
        }

        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        out[s] = (int16_t)mix;
    }
    return n;
}
