/* Synthetic showcase signals for RetroScope screenshots (float32 stereo, 48 kHz, stdout).
 *
 *   signal vector SECONDS   A-minor triad, slightly different phase and partial mix per
 *                           channel: a moving stereo figure like real music
 *   signal wave SECONDS     150 Hz band-limited sawtooth (12 harmonics)
 *   signal xy SECONDS       3:2 Lissajous figure that slowly rotates
 *
 * Self-generated test signals only; no recordings are involved. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define RATE 48000
static const double TAU = 6.283185307179586;

static void sample(const char *mode, double t, float out[2]) {
    double l = 0, r = 0;
    if (!strcmp(mode, "vector")) {
        static const double f[3] = {220.0, 261.63, 329.63};
        for (int k = 0; k < 3; ++k) {
            double drift = 0.7 * sin(TAU * (0.13 + 0.07 * k) * t + k);
            l += (0.30 - 0.05 * k) * sin(TAU * f[k] * t);
            r += (0.22 + 0.04 * k) * sin(TAU * f[k] * t + drift);
        }
        l += 0.08 * sin(TAU * 440.0 * t); r += 0.08 * sin(TAU * 523.25 * t);
    } else if (!strcmp(mode, "wave")) {
        for (int k = 1; k <= 12; ++k) l += sin(TAU * 150.0 * k * t) / k;
        l *= 0.42; r = l;
    } else { /* xy */
        l = 0.78 * sin(TAU * 300.0 * t);
        r = 0.78 * sin(TAU * 200.0 * t + TAU * 0.05 * t);
    }
    out[0] = (float)l; out[1] = (float)r;
}

int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[1], "vector") && strcmp(argv[1], "wave") && strcmp(argv[1], "xy"))) {
        fprintf(stderr, "usage: %s vector|wave|xy SECONDS\n", argv[0]);
        return 2;
    }
    const long frames = (long)(atof(argv[2]) * RATE);
    float block[480][2];
    for (long done = 0; done < frames;) {
        long n = frames - done < 480 ? frames - done : 480;
        for (long i = 0; i < n; ++i) sample(argv[1], (double)(done + i) / RATE, block[i]);
        if (fwrite(block, sizeof block[0], (size_t)n, stdout) != (size_t)n) return 1;
        done += n;
    }
    return fflush(stdout) != 0;
}
