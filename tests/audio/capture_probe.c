/* Capture probe for tests/audio/smoke.sh.
 *
 *   capture_probe tone SECONDS   write a synthetic float32 stereo test tone to stdout
 *                                 (L = 0.5 sin(2pi 440 t), R = -0.25 sin(2pi 440 t))
 *   capture_probe capture SECONDS open the production capture path (audio_open) and
 *                                 report frame count, per-channel RMS and L/R correlation
 *
 * The probe links RetroScope's real capture code; only the SDL frontend is omitted. */
#define main retroscope_main
#include "../../src/retroscope.c"
#undef main

static int tone(double seconds) {
    const size_t frames = (size_t)(seconds * SAMPLE_RATE);
    float block[480][2];
    for (size_t done = 0; done < frames;) {
        size_t n = frames - done < 480 ? frames - done : 480;
        for (size_t i = 0; i < n; ++i) {
            double s = sin(2 * 3.141592653589793 * 440 * (double)(done + i) / SAMPLE_RATE);
            block[i][0] = (float)(.5 * s);
            block[i][1] = (float)(-.25 * s);
        }
        if (fwrite(block, sizeof block[0], n, stdout) != n) return 1;
        done += n;
    }
    return fflush(stdout) != 0;
}

static int capture_report(double seconds) {
    Audio audio = {0};
    signal(SIGINT, stop_signal); signal(SIGTERM, stop_signal);
    if (audio_open(&audio) < 0) {
        fprintf(stderr, "probe: open failed: %s\n", audio.error);
        audio_close(&audio);
        return 2;
    }
    double sum_l = 0, sum_r = 0, sum_lr = 0;
    unsigned long long frames = 0, nonfinite = 0;
    int failed = 0;
    Uint64 start = SDL_GetTicks64();
    while (!interrupted && SDL_GetTicks64() - start < (Uint64)(seconds * 1000)) {
        pa_threaded_mainloop_lock(audio.loop);
        size_t count = audio.count, first = (audio.head + CAPACITY - count) % CAPACITY;
        for (size_t i = 0; i < count; ++i) {
            float l = audio.samples[(first + i) % CAPACITY][0], r = audio.samples[(first + i) % CAPACITY][1];
            if (!isfinite(l) || !isfinite(r)) ++nonfinite;
            sum_l += (double)l * l; sum_r += (double)r * r; sum_lr += (double)l * r;
        }
        frames += count;
        audio.count = 0;
        failed = audio.failed;
        pa_threaded_mainloop_unlock(audio.loop);
        if (failed) break;
        SDL_Delay(10);
    }
    double rms_l = frames ? sqrt(sum_l / frames) : 0, rms_r = frames ? sqrt(sum_r / frames) : 0;
    double corr = rms_l > 0 && rms_r > 0 ? sum_lr / frames / (rms_l * rms_r) : 0;
    printf("frames=%llu rms_l=%.4f rms_r=%.4f corr=%.4f nonfinite=%llu failed=%d\n",
           frames, rms_l, rms_r, corr, nonfinite, failed);
    if (failed) fprintf(stderr, "probe: %s\n", audio.error);
    audio_close(&audio);
    return failed ? 3 : 0;
}

int main(int argc, char **argv) {
    double seconds = argc == 3 ? strtod(argv[2], NULL) : 0;
    if (seconds <= 0 || seconds > 600) { fputs("usage: capture_probe tone|capture SECONDS\n", stderr); return 64; }
    if (!strcmp(argv[1], "tone")) return tone(seconds);
    if (!strcmp(argv[1], "capture")) return capture_report(seconds);
    fputs("usage: capture_probe tone|capture SECONDS\n", stderr);
    return 64;
}
