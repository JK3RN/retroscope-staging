#define main retroscope_main
#include "../src/retroscope.c"
#undef main
#include <assert.h>

static void mode_checks(void) {
    Settings settings={3,2};
    ScopeState state={0};
    const float stereo[][2]={{.25f,.75f},{-.5f,.5f}};
    scope_update(&state,stereo,2,1,.017f);
    assert(state.signal_hold>0 && state.clip_hold==0);
    assert(state.wave[0]==.5f && state.wave[1]==0);
    assert(select_mode(&state,SDLK_2) && state.mode==SCOPE_WAVE);
    assert(!select_mode(&state,SDLK_2));
    assert(settings.gain_step==3 && settings.persistence==2);
    SDL_Rect crt={0,0,400,200}; SDL_FPoint points[CAPACITY];
    assert(scope_points(&state,stereo,2,1,crt,points)==wave_windows[state.timebase]);
    assert(fabsf(points[0].x-24)<.001f && fabsf(points[959].x-376)<.001f);
    assert(points[958].y==56 && points[959].y==100);
    for(size_t i=1;i<960;++i) assert(points[i].x>points[i-1].x);
    assert(scope_points(&state,NULL,0,1,crt,points)==0);
    assert(sample_clips(1,-1,2,SCOPE_VECTOR));
    assert(!sample_clips(1,-1,2,SCOPE_WAVE));
    assert(!sample_clips(1,1,1,SCOPE_WAVE)); /* At the boundary, not clipped. */
    assert(sample_clips(.1f,.1f,16,SCOPE_WAVE));
    float samples[CAPACITY][2];
    for(size_t i=0;i<CAPACITY;++i) {samples[i][0]=(float)i; samples[i][1]=(float)i;}
    scope_update(&state,(const float (*)[2])samples,CAPACITY,1,.017f);
    assert(state.count==CAPACITY+2);
    size_t first=(state.head+WAVE_FRAMES-state.count)%WAVE_FRAMES;
    assert(state.wave[first]==.5f);
    assert(state.wave[(state.head+WAVE_FRAMES-1)%WAVE_FRAMES]==CAPACITY-1);
    assert(state.clip_hold>0);
    assert(select_mode(&state,SDLK_1) && state.clip_hold==0 && state.count==CAPACITY+2);
    assert(settings.gain_step==3 && settings.persistence==2);
    scope_update(&state,NULL,0,1,.2f);
    assert(state.signal_hold==0 && state.clip_hold==0 && state.count==0);
    select_mode(&state,SDLK_2);
    const float inverse[][2]={{.8f,-.8f}};
    scope_update(&state,inverse,1,16,.017f);
    assert(state.signal_hold>0 && state.clip_hold==0 && state.wave[0]==0);
    /* Samples overwritten before a WAVE sweep must not produce a false CLIP. */
    state.head=state.count=0;
    memset(samples,0,sizeof samples); samples[0][0]=samples[0][1]=4;
    scope_update(&state,(const float (*)[2])samples,CAPACITY,1,.2f);
    assert(state.clip_hold==0);
    for(int i=0;i<2000;++i) {
        select_mode(&state,(SDL_Keycode[]){SDLK_1,SDLK_2,SDLK_3}[i%3]);
        select_timebase(&state,SDLK_b);
        scope_update(&state,stereo,2,1,.017f);
    }
    assert(state.count==WAVE_FRAMES && state.head<WAVE_FRAMES);
}
static void xy_checks(void) {
    const int sizes[][2]={{400,400},{800,300},{300,800}};
    ScopeState state={.mode=SCOPE_XY}; SDL_FPoint points[CAPACITY];
    float samples[360][2];
    for(size_t size=0;size<3;++size) {
        SDL_Rect crt={0,0,sizes[size][0],sizes[size][1]};
        ScopeGeometry g=scope_geometry(crt,SCOPE_XY);
        assert(g.half_x==g.half_y);
        for(int shape=0;shape<6;++shape) {
            for(int i=0;i<360;++i) {
                float phase=(float)(i*2*3.141592653589793/360);
                float l=.7f*cosf(phase),r=.7f*sinf(phase);
                if(shape==0) r=0;
                if(shape==1) l=0;
                if(shape==2) r=l;
                if(shape==3) r=-l;
                if(shape==5) r*=.5f;
                samples[i][0]=l; samples[i][1]=r;
            }
            assert(scope_points(&state,(const float (*)[2])samples,360,1,crt,points)==360);
            float sumx=0,sumy=0;
            for(int i=0;i<360;++i) {
                float x=points[i].x-g.cx,y=g.cy-points[i].y;
                sumx+=x; sumy+=y;
                assert(fabsf(x-samples[i][0]*g.half_x)<.0001f);
                assert(fabsf(y-samples[i][1]*g.half_y)<.0001f);
                if(shape==0) assert(y==0);
                if(shape==1) assert(x==0);
                if(shape==2) assert(fabsf(x-y)<.0001f);
                if(shape==3) assert(fabsf(x+y)<.0001f);
                if(shape>=4) {
                    float yy=shape==5 ? y*2 : y;
                    assert(fabsf(hypotf(x,yy)-.7f*g.half_x)<.0001f);
                }
            }
            assert(fabsf(sumx/360)<.0001f && fabsf(sumy/360)<.0001f);
            if(shape>=4) {
                assert(points[0].x>g.cx && fabsf(points[0].y-g.cy)<.0001f);
                assert(points[90].y<g.cy); /* counterclockwise, positive Y up */
            }
        }
        const float clip[][2]={{2,-2}};
        scope_points(&state,clip,1,1,crt,points);
        assert(points[0].x==g.cx+g.half_x && points[0].y==g.cy+g.half_y);
    }
    assert(!sample_clips(1,-1,1,SCOPE_XY));
    assert(sample_clips(1,0,2,SCOPE_XY));
    assert(sample_clips(0,-1,2,SCOPE_XY));
    const float over[][2]={{0,1.5f}};
    scope_update(&state,over,1,1,.017f);
    assert(state.signal_hold>0 && state.clip_hold>0);
    scope_update(&state,NULL,0,1,.2f);
    assert(state.signal_hold==0 && state.clip_hold==0 && state.count==0);
    assert(select_mode(&state,SDLK_KP_1));
    assert(select_mode(&state,SDLK_KP_3));
}
static void trigger_checks(void) {
    ScopeState state={.mode=SCOPE_WAVE};
    float samples[CAPACITY][2]; SDL_FPoint points[CAPACITY];
    SDL_Rect crt={0,0,400,400};
    for(unsigned tb=0;tb<3;++tb) {
        state.timebase=tb; state.head=state.count=0;
        for(int block=0;block<4;++block) {
            for(size_t i=0;i<CAPACITY;++i)
                samples[i][0]=samples[i][1]=.7f*sinf((float)((block*CAPACITY+i)*2*3.141592653589793/160));
            scope_update(&state,(const float (*)[2])samples,CAPACITY,1,.017f);
            size_t start=wave_start(&state);
            assert(start>0 && start+wave_windows[tb]<=state.count);
            assert(wave_sample(&state,start-1)<=0 && wave_sample(&state,start)>0);
            assert(scope_points(&state,(const float (*)[2])samples,CAPACITY,1,crt,points)==wave_windows[tb]);
            assert(fabsf(points[0].y-200)<5); /* phase stable to one sample */
        }
        for(size_t i=0;i<CAPACITY;++i) samples[i][0]=samples[i][1]=.25f;
        for(int i=0;i<2;++i) scope_update(&state,(const float (*)[2])samples,CAPACITY,1,.017f);
        assert(wave_start(&state)==state.count-wave_windows[tb]); /* DC fallback */
        memset(samples,0,sizeof samples);
        for(int i=0;i<2;++i) scope_update(&state,(const float (*)[2])samples,CAPACITY,1,.2f);
        assert(state.signal_hold==0 && state.clip_hold==0);
        assert(wave_start(&state)==state.count-wave_windows[tb]);
    }
    /* CLIP follows the triggered sweep, including older samples, but excludes
       overrange samples outside the selected sweep. */
    state.timebase=1; state.count=WAVE_FRAMES; state.head=0;
    for(size_t i=0;i<WAVE_FRAMES;++i) state.wave[i]=.25f;
    size_t crossing=WAVE_FRAMES-wave_windows[1]-100;
    state.wave[crossing-1]=-.25f; state.wave[crossing+1]=2;
    const float fresh[][2]={{.25f,.25f}};
    scope_update(&state,fresh,1,1,.2f);
    assert(state.clip_hold>0);
    state.wave[crossing+1]=.25f;
    state.wave[(state.head+WAVE_FRAMES-1)%WAVE_FRAMES]=2;
    scope_update(&state,fresh,1,1,.2f);
    assert(state.clip_hold==0);
    state.timebase=2;
    assert(select_timebase(&state,SDLK_b) && state.timebase==0);
    select_mode(&state,SDLK_3); assert(!select_timebase(&state,SDLK_b));
}
static Uint32 color_at(SDL_Surface *s,int x,int y) {
    return ((Uint32*)((char*)s->pixels+y*s->pitch))[x];
}
static void render_checks(void) {
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);
    SDL_Window *window=SDL_CreateWindow("scope modes test",0,0,480,480,SDL_WINDOW_RESIZABLE);
    assert(window);
    SDL_Renderer *r=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE|SDL_RENDERER_TARGETTEXTURE);
    assert(r);
    const int sizes[][2]={{200,200},{480,480},{800,300},{300,800},{960,960}};
    const int gains[]={GAIN_MIN_STEP,0,GAIN_MAX_STEP};
    float samples[CAPACITY][2];
    for(size_t size=0;size<sizeof sizes/sizeof sizes[0];++size) {
        int w=sizes[size][0],h=sizes[size][1];
        SDL_SetWindowSize(window,w,h);
        SDL_Event e; while(SDL_PollEvent(&e)) {}
        SDL_SetRenderTarget(r,NULL); SDL_RenderClear(r); SDL_RenderPresent(r);
        int rw,rh; SDL_GetRendererOutputSize(r,&rw,&rh); assert(rw==w && rh==h);
        Layout layout=instrument_layout(w,h,size==4 ? 480 : w);
        SDL_Texture *face=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,w,h);
        SDL_Texture *trail=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,layout.crt.w,layout.crt.h);
        assert(face && trail); SDL_SetTextureBlendMode(trail,SDL_BLENDMODE_ADD);
        SDL_Surface *base=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
        SDL_Surface *screen=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
        assert(base && screen);
        for(int mode=0;mode<3;++mode) for(int gain=0;gain<3;++gain) for(int persistence=0;persistence<4;++persistence) for(unsigned tb=0;tb<3;++tb) {
            ScopeState state={.mode=(ScopeMode)mode}; Settings settings={gains[gain],persistence};
            state.timebase=tb;
            SDL_SetRenderTarget(r,face); faceplate(r,w,h,layout,&settings,state.mode,state.timebase);
            SDL_SetRenderTarget(r,NULL); SDL_RenderCopy(r,face,NULL,NULL);
            assert(SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_RGBA32,base->pixels,base->pitch)==0);
            for(int signal=0;signal<10;++signal) {
                state.head=state.count=0; state.signal_hold=state.clip_hold=0;
                for(size_t i=0;i<CAPACITY;++i) {
                    float phase=(float)(2*3.141592653589793*i/160);
                    float l=.7f*sinf(phase),rr=.7f*sinf(phase+.7f);
                    if(signal==0) l=rr=0;
                    if(signal==1) rr=l;
                    if(signal==2) rr=-l;
                    if(signal==3) { l*=.005f; rr*=.005f; }
                    if(signal==4) { l*=4; rr*=4; }
                    if(signal==5) rr=.4f*sinf(phase*1.3f);
                    if(signal==6) {l=.4f*sinf(phase)+.25f*sinf(phase*3);rr=.3f*sinf(phase*.93f)+.2f*cosf(phase*7);}
                    if(signal==7) rr=0;
                    if(signal==8) l=0;
                    if(signal==9) {l=.7f*cosf(phase);rr=.7f*sinf(phase);}
                    samples[i][0]=l; samples[i][1]=rr;
                }
                SDL_SetRenderTarget(r,trail); SDL_SetRenderDrawColor(r,0,0,0,255); SDL_RenderClear(r);
                scope_update(&state,(const float (*)[2])samples,CAPACITY,visual_gain(&settings),.017f);
                signal_trace(r,layout.crt,&settings,&state,(const float (*)[2])samples,CAPACITY,.017f);
                scope_update(&state,NULL,0,visual_gain(&settings),.1f);
                signal_trace(r,layout.crt,&settings,&state,NULL,0,.1f);
                scope_update(&state,(const float (*)[2])samples,CAPACITY,visual_gain(&settings),.017f);
                signal_trace(r,layout.crt,&settings,&state,(const float (*)[2])samples,CAPACITY,.017f);
                SDL_SetRenderTarget(r,NULL); SDL_RenderCopy(r,face,NULL,NULL); SDL_RenderCopy(r,trail,NULL,&layout.crt);
                assert(SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_RGBA32,screen->pixels,screen->pitch)==0);
                /* Before LEDs: signal must not spill into any frontplate pixel. */
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    if(x<layout.crt.x || x>=layout.crt.x+layout.crt.w || y<layout.crt.y || y>=layout.crt.y+layout.crt.h)
                        assert(color_at(base,x,y)==color_at(screen,x,y));
                status_lights(r,layout,&state); SDL_RenderPresent(r);
            }
        }
        for(int mode=0;mode<3;++mode) {
            ScopeState state={.mode=(ScopeMode)mode}; Settings settings={0,1};
            SDL_SetRenderTarget(r,face); faceplate(r,w,h,layout,&settings,state.mode,state.timebase);
            SDL_SetRenderTarget(r,trail); SDL_SetRenderDrawColor(r,0,0,0,255); SDL_RenderClear(r);
            for(int frame=0;frame<6;++frame) {
                for(size_t i=0;i<800;++i) {
                    float phase=(float)(2*3.141592653589793*(frame*800+i)/160);
                    samples[i][0]=.65f*sinf(phase);
                    samples[i][1]=.65f*sinf(phase+(mode==SCOPE_WAVE ? 0 : .6f+frame*.06f));
                }
                scope_update(&state,(const float (*)[2])samples,800,1,.017f);
                signal_trace(r,layout.crt,&settings,&state,(const float (*)[2])samples,800,.017f);
            }
            SDL_SetRenderTarget(r,NULL); SDL_RenderCopy(r,face,NULL,NULL); SDL_RenderCopy(r,trail,NULL,&layout.crt);
            status_lights(r,layout,&state);
            SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_RGBA32,screen->pixels,screen->pitch);
            char path[100]; snprintf(path,sizeof path,"/tmp/retroscope-%s-%dx%d.bmp",mode_names[mode],w,h);
            assert(SDL_SaveBMP(screen,path)==0);
        }
        SDL_FreeSurface(base); SDL_FreeSurface(screen); SDL_DestroyTexture(face); SDL_DestroyTexture(trail);
    }
    SDL_DestroyRenderer(r); SDL_DestroyWindow(window); SDL_Quit();
}
/* Optional long run uses the production event loop, toggling every second. */
/* Regression: the --demo signal used wall-clock time per frame, so block boundaries
   jumped in phase whenever a frame did not last exactly DEMO_BLOCK samples and WAVE
   showed doubled traces. Blocks must now join seamlessly and equal one long run. */
static void demo_checks(void) {
    static float one[DEMO_BLOCK*4][2], parts[DEMO_BLOCK*4][2];
    demo_signal(12345,one,DEMO_BLOCK*4);
    for(size_t b=0;b<4;++b) demo_signal(12345+b*DEMO_BLOCK,parts+b*DEMO_BLOCK,DEMO_BLOCK);
    assert(!memcmp(one,parts,sizeof one));
    /* 220 Hz at amplitude .75: no sample step may exceed the analytic maximum slope. */
    const float max_step=.75f*2*3.14159265f*220/SAMPLE_RATE*1.01f;
    for(size_t i=1;i<DEMO_BLOCK*4;++i) assert(fabsf(one[i][0]-one[i-1][0])<=max_step);
    /* Fed block by block, the triggered WAVE sweep is a clean sine as well. */
    ScopeState state={.mode=SCOPE_WAVE};
    unsigned long long position=0;
    for(int frame=0;frame<8;++frame) {
        float block[DEMO_BLOCK][2];
        demo_signal(position,block,DEMO_BLOCK); position+=DEMO_BLOCK;
        scope_update(&state,(const float (*)[2])block,DEMO_BLOCK,1,1/60.f);
    }
    size_t start=wave_start(&state);
    for(size_t i=start+1;i<start+wave_windows[0];++i)
        assert(fabsf(wave_sample(&state,i)-wave_sample(&state,i-1))<=max_step);
}
static Uint32 switch_timer(Uint32 interval, void *userdata) {
    unsigned *step=userdata;
    SDL_Event event={.type=SDL_KEYDOWN}; event.key.keysym.sym=(SDL_Keycode[]){SDLK_1,SDLK_2,SDLK_b,SDLK_b,SDLK_3}[(*step)++%5];
    assert(SDL_PushEvent(&event)==1);
    return interval;
}
int main(int argc,char **argv) {
    if(argc==3 && !strcmp(argv[1],"--soak")) {
        SDL_Init(SDL_INIT_TIMER); unsigned step=0;
        assert(SDL_AddTimer(1000,switch_timer,&step));
        char *args[]={"retroscope","--demo","--seconds",argv[2],NULL};
        return retroscope_main(4,args);
    }
    mode_checks(); xy_checks(); trigger_checks(); demo_checks(); render_checks();
    puts("VECTOR/WAVE/XY geometry and trigger/timebase, mono mix, continuous demo signal, history bounds, true clipping, status decay and render matrix passed");
    return 0;
}
