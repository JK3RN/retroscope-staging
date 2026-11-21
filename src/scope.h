#ifndef RETROSCOPE_SCOPE_H
#define RETROSCOPE_SCOPE_H

#define WAVE_FRAMES 3840 /* 80 ms: longest sweep plus trigger search reserve. */
_Static_assert(SAMPLE_RATE/25 <= CAPACITY, "WAVE sweep must fit the point buffer");
static const size_t wave_windows[] = {SAMPLE_RATE/50, SAMPLE_RATE/200, SAMPLE_RATE/25};
static const char *const timebase_names[] = {"20MS", "5MS", "40MS"};
typedef enum { SCOPE_VECTOR, SCOPE_WAVE, SCOPE_XY } ScopeMode;
static const char *const mode_names[] = { "VECTOR", "WAVE", "XY" };
typedef struct {
    ScopeMode mode;
    unsigned timebase;
    float wave[WAVE_FRAMES];
    size_t head, count;
    float signal_hold, clip_hold;
} ScopeState;
typedef struct { float cx, cy, half_x, half_y; } ScopeGeometry;

static ScopeGeometry scope_geometry(SDL_Rect crt, ScopeMode mode) {
    float radius=fminf(crt.w,crt.h)*.44f;
    return (ScopeGeometry){crt.w*.5f,crt.h*.5f,
        mode!=SCOPE_WAVE ? radius : crt.w*.44f,
        mode!=SCOPE_WAVE ? radius : crt.h*.44f};
}
static int select_mode(ScopeState *state, SDL_Keycode key) {
    ScopeMode mode=state->mode;
    if(key==SDLK_1 || key==SDLK_KP_1) mode=SCOPE_VECTOR;
    if(key==SDLK_2 || key==SDLK_KP_2) mode=SCOPE_WAVE;
    if(key==SDLK_3 || key==SDLK_KP_3) mode=SCOPE_XY;
    if(mode==state->mode) return 0;
    state->mode=mode;
    state->clip_hold=0; /* Clipping belongs to the selected projection. */
    return 1;
}
static int sample_clips(float l, float r, float gain, ScopeMode mode) {
    if(mode==SCOPE_XY) return fabsf(l*gain)>1 || fabsf(r*gain)>1;
    float mid=(l*.5f+r*.5f)*gain;
    return fabsf(mid)>1 || (mode==SCOPE_VECTOR && fabsf((l*.5f-r*.5f)*gain)>1);
}
static int select_timebase(ScopeState *state, SDL_Keycode key) {
    if(state->mode!=SCOPE_WAVE || key!=SDLK_b) return 0;
    state->timebase=(state->timebase+1)%3;
    state->clip_hold=0;
    return 1;
}
static float wave_sample(const ScopeState *state, size_t i) {
    return state->wave[(state->head+WAVE_FRAMES-state->count+i)%WAVE_FRAMES];
}
/* Latest rising zero crossing with a complete sweep after it. If none exists,
   use the most recent window; startup is left-padded with silence. */
static size_t wave_start(const ScopeState *state) {
    size_t window=wave_windows[state->timebase];
    if(state->count<=window) return 0;
    size_t latest=state->count-window;
    for(size_t i=latest;i>0;--i)
        if(wave_sample(state,i-1)<=0 && wave_sample(state,i)>0) return i;
    return latest;
}
static void scope_update(ScopeState *state, const float samples[][2], size_t count,
                         float gain, float elapsed) {
    state->signal_hold=fmaxf(0,state->signal_hold-elapsed);
    state->clip_hold=fmaxf(0,state->clip_hold-elapsed);
    for(size_t i=0;i<count;++i) {
        float l=samples[i][0],r=samples[i][1];
        state->wave[state->head]=l*.5f+r*.5f;
        state->head=(state->head+1)%WAVE_FRAMES;
        if(state->count<WAVE_FRAMES) ++state->count;
        if(fmaxf(fabsf(l),fabsf(r))>1e-4f) state->signal_hold=.12f;
        if(state->mode!=SCOPE_WAVE && sample_clips(l,r,gain,state->mode)) state->clip_hold=.12f;
    }
    if(!count && state->signal_hold==0) state->head=state->count=0;
    if(count && state->mode==SCOPE_WAVE) {
        size_t start=wave_start(state), window=wave_windows[state->timebase];
        for(size_t i=start;i<state->count && i<start+window;++i)
            if(fabsf(wave_sample(state,i)*gain)>1) state->clip_hold=.12f;
    }
}
static size_t scope_points(const ScopeState *state, const float samples[][2], size_t count,
                           float gain, SDL_Rect crt, SDL_FPoint points[CAPACITY]) {
    if(state->mode!=SCOPE_WAVE) {
        ScopeGeometry g=scope_geometry(crt,state->mode);
        for(size_t i=0;i<count;++i) {
            if(state->mode==SCOPE_VECTOR)
                points[i]=project(samples[i][0]*gain,samples[i][1]*gain,crt.w,crt.h);
            else points[i]=(SDL_FPoint){g.cx+clamp(samples[i][0]*gain)*g.half_x,
                                        g.cy-clamp(samples[i][1]*gain)*g.half_y};
        }
        return count;
    }
    if(!count) return 0; /* Never re-brighten old audio when capture goes idle. */
    ScopeGeometry g=scope_geometry(crt,SCOPE_WAVE);
    size_t window=wave_windows[state->timebase];
    size_t padding=state->count<window ? window-state->count : 0;
    size_t first=wave_start(state);
    for(size_t i=0;i<window;++i) {
        float sample=i<padding ? 0 : wave_sample(state,first+i-padding);
        points[i]=(SDL_FPoint){g.cx-g.half_x+2*g.half_x*i/(window-1),
                              g.cy-clamp(sample*gain)*g.half_y};
    }
    return window;
}
#endif
