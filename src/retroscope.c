#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <pulse/pulseaudio.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RETROSCOPE_VERSION
#define RETROSCOPE_VERSION "0.1.0"
#endif
#define CAPACITY 2048
#define SAMPLE_RATE 48000
static volatile sig_atomic_t interrupted;
static void stop_signal(int sig) { (void)sig; interrupted = 1; }
typedef struct {
    pa_threaded_mainloop *loop;
    pa_context *context;
    pa_stream *stream;
    float samples[CAPACITY][2];
    size_t head, count;
    int ready, failed;
    char error[256];
} Audio;
static void fail(Audio *a, const char *message) {
    if (a->failed) return;
    a->failed = 1;
    snprintf(a->error, sizeof a->error, "%s: %s", message,
             pa_strerror(pa_context_errno(a->context)));
}
static void buffer_samples(Audio *a, const float *p, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        a->samples[a->head][0] = p && isfinite(p[2*i]) ? p[2*i] : 0;
        a->samples[a->head][1] = p && isfinite(p[2*i+1]) ? p[2*i+1] : 0;
        a->head = (a->head + 1) % CAPACITY;
        if (a->count < CAPACITY) ++a->count;
    }
}
static void capture(pa_stream *s, size_t bytes, void *userdata) {
    (void)bytes;
    Audio *a = userdata;
    const void *data;
    size_t n;
    if (pa_stream_peek(s, &data, &n) < 0) { fail(a, "Audio konnte nicht gelesen werden"); return; }
    buffer_samples(a, data, n / (2 * sizeof(float)));
    if (n && pa_stream_drop(s) < 0) fail(a, "Audio-Puffer konnte nicht freigegeben werden");
}
static void stream_state(pa_stream *s, void *userdata) {
    Audio *a = userdata;
    switch (pa_stream_get_state(s)) {
    case PA_STREAM_READY: a->ready = 1; break;
    case PA_STREAM_FAILED: case PA_STREAM_TERMINATED: fail(a, "Audio-Monitor getrennt"); break;
    default: break;
    }
}
static void sink_info(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    Audio *a = userdata;
    if (eol) { if (!a->stream) fail(a, "Kein geeigneter Output-Monitor gefunden"); return; }
    if (!info || !info->monitor_source_name) { fail(a, "Output hat keinen Audio-Monitor"); return; }
    pa_sample_spec spec = { PA_SAMPLE_FLOAT32NE, SAMPLE_RATE, 2 };
    pa_channel_map map;
    pa_channel_map_init_stereo(&map);
    a->stream = pa_stream_new(c, "Stereo output monitor", &spec, &map);
    if (!a->stream) { fail(a, "Audio-Stream konnte nicht erstellt werden"); return; }
    pa_stream_set_read_callback(a->stream, capture, a);
    pa_stream_set_state_callback(a->stream, stream_state, a);
    pa_buffer_attr attr = { .maxlength = SAMPLE_RATE * 8 / 10, .tlength = (uint32_t)-1,
        .prebuf = (uint32_t)-1, .minreq = (uint32_t)-1, .fragsize = 480 * 8 };
    if (pa_stream_connect_record(a->stream, info->monitor_source_name, &attr,
        PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE) < 0)
        fail(a, "Output-Monitor konnte nicht geöffnet werden");
    else fprintf(stderr, "RetroScope: Monitor %s, 48 kHz Stereo\n", info->monitor_source_name);
}
static void server_info(pa_context *c, const pa_server_info *info, void *userdata) {
    Audio *a = userdata;
    if (!info || !info->default_sink_name) { fail(a, "Kein Standard-Audioausgang gefunden"); return; }
    pa_operation *op = pa_context_get_sink_info_by_name(c, info->default_sink_name, sink_info, a);
    if (op) pa_operation_unref(op); else fail(a, "Audioausgang konnte nicht abgefragt werden");
}
static void context_state(pa_context *c, void *userdata) {
    Audio *a = userdata;
    if (pa_context_get_state(c) == PA_CONTEXT_READY) {
        pa_operation *op = pa_context_get_server_info(c, server_info, a);
        if (op) pa_operation_unref(op); else fail(a, "Audioserver konnte nicht abgefragt werden");
    } else if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(c))) fail(a, "Keine Verbindung zu PipeWire/PulseAudio");
}
static void audio_close(Audio *a) {
    if (!a->loop) return;
    pa_threaded_mainloop_stop(a->loop);
    if (a->stream) { pa_stream_disconnect(a->stream); pa_stream_unref(a->stream); }
    if (a->context) { pa_context_disconnect(a->context); pa_context_unref(a->context); }
    pa_threaded_mainloop_free(a->loop);
}
static int audio_open(Audio *a) {
    a->loop = pa_threaded_mainloop_new();
    if (!a->loop) return -1;
    a->context = pa_context_new(pa_threaded_mainloop_get_api(a->loop), "RetroScope");
    if (!a->context) return -1;
    pa_context_set_state_callback(a->context, context_state, a);
    if (pa_context_connect(a->context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0) {
        fail(a, "Keine Verbindung zu PipeWire/PulseAudio"); return -1;
    }
    if (pa_threaded_mainloop_start(a->loop) < 0) return -1;
    Uint64 start = SDL_GetTicks64();
    while (SDL_GetTicks64() - start < 5000 && !interrupted) {
        pa_threaded_mainloop_lock(a->loop);
        int ready = a->ready, failed = a->failed;
        pa_threaded_mainloop_unlock(a->loop);
        if (failed) return -1;
        if (ready) return 0;
        SDL_Delay(10);
    }
    pa_threaded_mainloop_lock(a->loop);
    if (!a->failed) {
        a->failed = 1;
        snprintf(a->error, sizeof a->error, "%s", interrupted ? "Abgebrochen"
                 : "Zeitüberschreitung beim Öffnen des Audio-Monitors");
    }
    pa_threaded_mainloop_unlock(a->loop);
    return -1;
}
static float clamp(float x) { return fmaxf(-1, fminf(1, x)); }
static SDL_FPoint project(float l, float r, int w, int h) {
    float scale = fminf(w, h) * .44f;
    return (SDL_FPoint){ w*.5f + clamp((l-r)*.5f)*scale,
                         h*.5f - clamp((l+r)*.5f)*scale };
}
#include "instrument.h"

/* Built-in --demo signal. Driven by a running sample position, not wall-clock time,
   so consecutive blocks join without phase jumps whatever the frame timing is. */
#define DEMO_BLOCK 800
static void demo_signal(unsigned long long position, float samples[][2], size_t count) {
    for (size_t i = 0; i < count; ++i) {
        double t = (double)(position + i) / SAMPLE_RATE;
        double phase = 2*3.141592653589793*220*t;
        samples[i][0] = .75f*(float)sin(phase);
        samples[i][1] = .75f*(float)sin(phase + .9*sin(t*.8));
    }
}

int main(int argc, char **argv) {
    int demo = 0;
    unsigned duration = 0;
    for (int i=1; i<argc; ++i) {
        if (!strcmp(argv[i], "--demo")) demo = 1;
        else if (!strcmp(argv[i], "--help")) {
            puts("RetroScope [--demo] [--seconds N] [--version]\n--demo: synthetisches Stereo-Signal, kein Audio-Capture\n--seconds N: begrenzter Testlauf\n--version: Version anzeigen");
            fputs(help_text, stdout); return 0;
        } else if (!strcmp(argv[i], "--version")) {
            puts("RetroScope " RETROSCOPE_VERSION); return 0;
        } else if (!strcmp(argv[i], "--seconds") && i+1>=argc) {
            fputs("--seconds benötigt eine Dauer in Sekunden (1 bis 86400)\n", stderr); return 1;
        } else if (!strcmp(argv[i], "--seconds")) {
            char *end; unsigned long n = strtoul(argv[++i], &end, 10);
            if (*end || n == 0 || n > 86400) { fputs("Ungültige Testdauer\n", stderr); return 1; }
            duration = (unsigned)n;
        } else { fprintf(stderr, "Unbekanntes Argument: %s\n", argv[i]); return 1; }
    }
    signal(SIGINT, stop_signal); signal(SIGTERM, stop_signal);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) { fprintf(stderr,"SDL: %s\n",SDL_GetError()); return 1; }
    Audio audio = {0};
    SDL_Window *window = NULL; SDL_Renderer *renderer = NULL; SDL_Texture *trail = NULL, *face = NULL;
    Settings settings = { .gain_step = 0, .persistence = 1 };
    ScopeState scope = { .mode = SCOPE_VECTOR };
    Layout layout = {0};
    int trace_dirty = 1;
    int face_dirty = 1;
    int result = 1;
    if (!demo && audio_open(&audio) < 0) {
        if (interrupted) { result = 0; goto cleanup; } /* SIGINT/SIGTERM while connecting */
        fprintf(stderr,"RetroScope: %s. Läuft pipewire-pulse in dieser Benutzersitzung?\n", audio.error[0] ? audio.error : "Audio-Initialisierung fehlgeschlagen"); goto cleanup;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    window = SDL_CreateWindow("RetroScope", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              480, 480, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) goto sdl_error;
    SDL_SetWindowMinimumSize(window, 200, 200);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) goto sdl_error;
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) < 0) goto sdl_error;
    fprintf(stderr, "RetroScope: SDL-Renderer %s (%s, Rendertexturen %s)\n",
            info.name, info.flags & SDL_RENDERER_ACCELERATED ? "beschleunigter SDL-Pfad" : "Software",
            info.flags & SDL_RENDERER_TARGETTEXTURE ? "ja" : "nein");
    /* SDL's accelerated flag describes the API path, not the physical GPU.
       Mesa may provide OpenGL through llvmpipe, so report that too when available. */
    if (!strncmp(info.name,"opengl",6) && SDL_GL_GetCurrentContext()) {
        union {
            void *address;
            const unsigned char *(SDLCALL *get_string)(unsigned int);
        } gl = { .address = SDL_GL_GetProcAddress("glGetString") };
        if (gl.address) {
            const unsigned char *device = gl.get_string(0x1F01); /* GL_RENDERER */
            if (device) fprintf(stderr,"RetroScope: OpenGL-Gerät %s\n",device);
        }
    }
    if (!(info.flags & SDL_RENDERER_TARGETTEXTURE)) {
        fputs("RetroScope: Renderer unterstützt keine CRT-Rendertexturen\n", stderr); goto cleanup;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    Uint64 start = SDL_GetTicks64(), previous = start;
    int running = 1, width = 0, height = 0;
    unsigned long long demo_position = 0;
    while (running && !interrupted && (!duration || SDL_GetTicks64()-start < duration*1000ULL)) {
        Uint64 frame = SDL_GetTicks64();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_q || key == SDLK_ESCAPE) running = 0;
                if (key == SDLK_h) fputs(help_text,stderr);
                if (adjust_setting(&settings, key)) face_dirty = 1;
                if (select_timebase(&scope,key)) { face_dirty=1; trace_dirty=1; }
                if (select_mode(&scope,key)) { face_dirty=1; trace_dirty=1; }
            }
        }
        int w,h;
        if (SDL_GetRendererOutputSize(renderer,&w,&h) < 0) goto sdl_error;
        if (w <= 0 || h <= 0) { SDL_Delay(50); continue; }
        if (w != width || h != height) {
            int logical_w, logical_h;
            SDL_GetWindowSize(window,&logical_w,&logical_h);
            (void)logical_h;
            layout = instrument_layout(w,h,logical_w);
            SDL_SetRenderTarget(renderer,NULL);
            SDL_DestroyTexture(trail); SDL_DestroyTexture(face);
            trail = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,layout.crt.w,layout.crt.h);
            face = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,w,h);
            if (!trail || !face) goto sdl_error;
            SDL_SetTextureBlendMode(trail,SDL_BLENDMODE_ADD);
            width=w; height=h; face_dirty=1;
            if (SDL_SetRenderTarget(renderer,trail) < 0) goto sdl_error;
            SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
        }
        if (face_dirty) {
            if (SDL_SetRenderTarget(renderer,face) < 0) goto sdl_error;
            faceplate(renderer,w,h,layout,&settings,scope.mode,scope.timebase);
            face_dirty=0;
        }
        float samples[CAPACITY][2]; size_t count = 0;
        if (demo) {
            count=DEMO_BLOCK;
            demo_signal(demo_position,samples,count);
            demo_position+=count;
        } else {
            pa_threaded_mainloop_lock(audio.loop);
            int failed=audio.failed;
            count=audio.count;
            size_t first=(audio.head+CAPACITY-count)%CAPACITY;
            for(size_t i=0;i<count;++i) memcpy(samples[i],audio.samples[(first+i)%CAPACITY],sizeof samples[i]);
            audio.count=0;
            pa_threaded_mainloop_unlock(audio.loop);
            if (failed) { fprintf(stderr,"RetroScope: %s\n",audio.error); goto cleanup; }
        }
        if (SDL_SetRenderTarget(renderer,trail) < 0) goto sdl_error;
        float elapsed=(frame-previous)/1000.f; previous=frame;
        if(trace_dirty) {
            SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
            trace_dirty=0;
        }
        scope_update(&scope,(const float (*)[2])samples,count,visual_gain(&settings),elapsed);
        signal_trace(renderer,layout.crt,&settings,&scope,(const float (*)[2])samples,count,elapsed);
        if (SDL_SetRenderTarget(renderer,NULL) < 0) goto sdl_error;
        if (SDL_RenderCopy(renderer,face,NULL,NULL) < 0 ||
            SDL_RenderCopy(renderer,trail,NULL,&layout.crt) < 0) goto sdl_error;
        status_lights(renderer,layout,&scope);
        SDL_RenderPresent(renderer);
        Uint64 spent=SDL_GetTicks64()-frame;
        if(spent<17) SDL_Delay((Uint32)(17-spent));
    }
    result=0; goto cleanup;
sdl_error:
    fprintf(stderr,"RetroScope SDL: %s\n",SDL_GetError());
cleanup:
    audio_close(&audio);
    SDL_DestroyTexture(trail); SDL_DestroyTexture(face); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    SDL_Quit(); return result;
}
