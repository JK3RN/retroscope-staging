#define main retroscope_main
#include "../src/retroscope.c"
#undef main
#include <assert.h>

static void settings_checks(void) {
    Settings s={0,1};
    for(int i=0;i<1000;++i) adjust_setting(&s,SDLK_UP);
    assert(s.gain_step==GAIN_MAX_STEP && visual_gain(&s)==16.f);
    assert(!adjust_setting(&s,SDLK_PLUS));
    for(int i=0;i<1000;++i) adjust_setting(&s,SDLK_DOWN);
    assert(s.gain_step==GAIN_MIN_STEP && visual_gain(&s)==.25f);
    adjust_setting(&s,SDLK_0); assert(visual_gain(&s)==1.f);
    adjust_setting(&s,SDLK_KP_PLUS); assert(visual_gain(&s)>1.f);
    adjust_setting(&s,SDLK_KP_MINUS); assert(visual_gain(&s)==1.f);
    for(int i=0;i<1000;++i) {
        adjust_setting(&s,SDLK_RIGHT);
        adjust_setting(&s,SDLK_UP);
        adjust_setting(&s,SDLK_LEFT);
        adjust_setting(&s,SDLK_DOWN);
    }
    assert(s.persistence==1 && visual_gain(&s)==1.f);
    for(int i=0;i<20;++i) adjust_setting(&s,SDLK_LEFT);
    assert(s.persistence==0 && !adjust_setting(&s,SDLK_LEFT));
    Uint8 last=255;
    for(int i=0;i<4;++i) {
        s.persistence=i;
        Uint8 alpha=fade_alpha(.017f,&s);
        assert(alpha<last); last=alpha;
        assert(fade_alpha(5.f,&s)>=254);
        /* Equivalent exponential decay at 30 and 60 Hz, allowing quantization. */
        float a=1-fade_alpha(1.f/30,&s)/255.f;
        float b=1-fade_alpha(1.f/60,&s)/255.f;
        assert(fabsf(a-b*b)<.012f);
    }
    for(int i=0;i<20;++i) adjust_setting(&s,SDLK_RIGHT);
    assert(s.persistence==3 && !adjust_setting(&s,SDLK_RIGHT));
}
static Uint32 pixel(SDL_Surface *s,int x,int y) {
    return ((Uint32*)((char*)s->pixels+y*s->pitch))[x];
}
static void decay_checks(SDL_Renderer *r) {
    SDL_Texture *trail=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,200,200);
    assert(trail);
    ScopeState state = {0};
    const float tone[][2]={{.5f,.5f},{.5f,.5f}};
    SDL_Rect crt={0,0,200,200};
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,200,200,32,SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_FPoint point=project(.5f,.5f,200,200);
    int px=(int)point.x,py=(int)point.y;
    int previous_green=-1;
    for(int i=0;i<4;++i) {
        Settings settings={0,i};
        assert(SDL_SetRenderTarget(r,trail)==0);
        SDL_SetRenderDrawColor(r,0,0,0,255); SDL_RenderClear(r);
        signal_trace(r,crt,&settings,&state,tone,2,.017f);
        SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_RGBA32,surface->pixels,surface->pitch);
        Uint8 red,green,blue,alpha;
        SDL_GetRGBA(pixel(surface,px,py),surface->format,&red,&green,&blue,&alpha);
        int original_green=green;
        assert(original_green>0);
        signal_trace(r,crt,&settings,&state,tone,0,.14f);
        SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_RGBA32,surface->pixels,surface->pitch);
        SDL_GetRGBA(pixel(surface,px,py),surface->format,&red,&green,&blue,&alpha);
        assert(green<original_green && green>previous_green);
        previous_green=green;
    }
    SDL_SetRenderTarget(r,NULL);
    SDL_FreeSurface(surface); SDL_DestroyTexture(trail);
}
static void rendering_checks(void) {
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);
    SDL_Window *window=SDL_CreateWindow("instrument test",0,0,480,480,SDL_WINDOW_RESIZABLE);
    assert(window);
    SDL_Renderer *r=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE|SDL_RENDERER_TARGETTEXTURE);
    assert(r);
    decay_checks(r);
    const int sizes[][2]={{200,200},{480,480},{800,300},{300,800},{960,960}};
    const int gains[]={GAIN_MIN_STEP,0,GAIN_MAX_STEP};
    ScopeState state = {0};
    float samples[800][2];
    for(size_t size=0;size<sizeof sizes/sizeof sizes[0];++size) {
        int w=sizes[size][0],h=sizes[size][1];
        SDL_SetWindowSize(window,w,h);
        SDL_Event resized; while(SDL_PollEvent(&resized)) {}
        SDL_SetRenderTarget(r,NULL);
        SDL_SetRenderDrawColor(r,0,0,0,255); SDL_RenderClear(r);
        SDL_RenderPresent(r);
        int out_w,out_h; assert(SDL_GetRendererOutputSize(r,&out_w,&out_h)==0);
        assert(out_w==w && out_h==h);
        Layout layout=instrument_layout(w,h,size==4 ? 480 : w);
        SDL_Rect crt=layout.crt;
        assert(crt.x>0 && crt.y>0 && crt.x+crt.w<w && crt.y+crt.h<h);
        assert((double)crt.w*crt.h/(w*h)>.80);
        assert((double)crt.w*crt.h/(w*h)<.95);
        /* Uniform x/y scale inside the CRT even for non-square windows. */
        SDL_FPoint center=project(0,0,crt.w,crt.h);
        SDL_FPoint mono=project(.5,.5,crt.w,crt.h);
        SDL_FPoint anti=project(.5,-.5,crt.w,crt.h);
        assert(fabsf((center.y-mono.y)-(anti.x-center.x))<.001f);
        SDL_Texture *face=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,w,h);
        SDL_Texture *trail=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,crt.w,crt.h);
        assert(face && trail);
        assert(SDL_SetTextureBlendMode(trail,SDL_BLENDMODE_ADD)==0);
        SDL_Surface *before=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
        SDL_Surface *after=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
        assert(before && after);
        for(int gain=0;gain<3;++gain) for(int persistence=0;persistence<4;++persistence) {
            Settings settings={gains[gain],persistence};
            assert(SDL_SetRenderTarget(r,face)==0);
            faceplate(r,w,h,layout,&settings,SCOPE_VECTOR,0);
            assert(SDL_SetRenderTarget(r,NULL)==0);
            assert(SDL_RenderCopy(r,face,NULL,NULL)==0);
            assert(SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_RGBA32,before->pixels,before->pitch)==0);
            for(int signal=0;signal<6;++signal) {
                for(int i=0;i<800;++i) {
                    double phase=2*3.141592653589793*i/160;
                    float l=(float)sin(phase), rr=(float)sin(phase+.7);
                    if(signal==0) l=rr=0;           /* silence */
                    if(signal==1) rr=l;            /* mono */
                    if(signal==2) rr=-l;           /* anti-phase */
                    if(signal==3) { l*=.005f; rr*=.005f; } /* quiet */
                    if(signal==4) { l*=2; rr*=2; }  /* loud/clipped */
                    if(signal==5) { l*=.7f; rr=.4f*(float)sin(phase*1.3); }
                    samples[i][0]=l; samples[i][1]=rr;
                }
                assert(SDL_SetRenderTarget(r,trail)==0);
                SDL_SetRenderDrawColor(r,0,0,0,255); assert(SDL_RenderClear(r)==0);
                signal_trace(r,crt,&settings,&state,(const float (*)[2])samples,800,.017f);
                /* Stop, persistence decay, and restart use the same texture. */
                signal_trace(r,crt,&settings,&state,(const float (*)[2])samples,0,.1f);
                signal_trace(r,crt,&settings,&state,(const float (*)[2])samples,800,.017f);
                assert(SDL_SetRenderTarget(r,NULL)==0);
                assert(SDL_RenderCopy(r,face,NULL,NULL)==0);
                assert(SDL_RenderCopy(r,trail,NULL,&crt)==0);
                assert(SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_RGBA32,after->pixels,after->pitch)==0);
                /* Signal and glow must not reach the instrument labels. */
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    if(x<crt.x || x>=crt.x+crt.w || y<crt.y || y>=crt.y+crt.h)
                        assert(pixel(before,x,y)==pixel(after,x,y));
                SDL_RenderPresent(r);
            }
        }
        /* Export the same renderer for visual inspection, at all sizes. */
        Settings settings={0,1};
        SDL_SetRenderTarget(r,face); faceplate(r,w,h,layout,&settings,SCOPE_VECTOR,0);
        SDL_SetRenderTarget(r,trail);
        SDL_SetRenderDrawColor(r,0,0,0,255); SDL_RenderClear(r);
        for(int frame=0;frame<8;++frame) {
            for(int i=0;i<800;++i) {
                float phase=(float)(2*3.141592653589793*i/160);
                samples[i][0]=.65f*sinf(phase);
                samples[i][1]=.65f*sinf(phase+.5f+frame*.09f);
            }
            signal_trace(r,crt,&settings,&state,(const float (*)[2])samples,800,.017f);
        }
        SDL_SetRenderTarget(r,NULL); SDL_RenderCopy(r,face,NULL,NULL); SDL_RenderCopy(r,trail,NULL,&crt);
        SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_RGBA32,after->pixels,after->pitch);
        /* Inspection images go to the build directory, never a shared /tmp path. */
        const char *out=getenv("RETROSCOPE_TEST_OUT");
        char path[512]; snprintf(path,sizeof path,"%s/retroscope-instrument-%dx%d.bmp",out ? out : ".",w,h);
        assert(SDL_SaveBMP(after,path)==0);
        SDL_FreeSurface(before); SDL_FreeSurface(after);
        SDL_DestroyTexture(face); SDL_DestroyTexture(trail);
    }
    SDL_DestroyRenderer(r); SDL_DestroyWindow(window); SDL_Quit();
}
static Uint32 controls_timer(Uint32 interval, void *userdata) {
    (void)interval;
    unsigned *step=userdata;
    SDL_Event event={0};
    if(*step<100) {
        const SDL_Keycode keys[]={SDLK_UP,SDLK_RIGHT,SDLK_2,SDLK_b,SDLK_b,SDLK_3,SDLK_DOWN,SDLK_LEFT,SDLK_1,SDLK_0};
        event.type=SDL_KEYDOWN; event.key.keysym.sym=keys[*step%(sizeof keys/sizeof keys[0])];
    } else event.type=SDL_QUIT;
    assert(SDL_PushEvent(&event)==1);
    ++*step;
    return *step<101 ? 5 : 0;
}
static void event_checks(void) {
    assert(SDL_Init(SDL_INIT_TIMER)==0);
    unsigned step=0;
    SDL_TimerID timer=SDL_AddTimer(5,controls_timer,&step);
    assert(timer);
    char *args[]={"retroscope","--demo","--seconds","5",NULL};
    assert(retroscope_main(4,args)==0);
    /* SDL_Quit in the application stops the timer before this read. */
    assert(step==101);
}
/* Regression: tiling WMs can assign tiles below SDL's minimum window size.
   The CRT must stay >= 1x1 so render-texture creation never aborts the app. */
static void tiny_layout_checks(void) {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    SDL_Window *window=SDL_CreateWindow("tiny",0,0,64,64,0);
    SDL_Renderer *r=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE|SDL_RENDERER_TARGETTEXTURE);
    assert(window && r);
    const int sizes[][3]={{120,60,120},{60,30,60},{30,30,30},{400,30,400},{12,12,12},{1,1,1},{60,30,30}};
    for(size_t i=0;i<sizeof sizes/sizeof sizes[0];++i) {
        int w=sizes[i][0],h=sizes[i][1];
        Layout layout=instrument_layout(w,h,sizes[i][2]);
        assert(layout.crt.w>=1 && layout.crt.h>=1);
        SDL_Texture *face=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,w,h);
        SDL_Texture *trail=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,layout.crt.w,layout.crt.h);
        assert(face && trail);
        Settings settings={0,1}; ScopeState state={.mode=SCOPE_WAVE};
        const float tone[][2]={{.5f,.5f},{-.5f,-.5f}};
        for(int mode=0;mode<3;++mode) {
            state.mode=(ScopeMode)mode;
            assert(SDL_SetRenderTarget(r,face)==0);
            faceplate(r,w,h,layout,&settings,state.mode,0);
            assert(SDL_SetRenderTarget(r,trail)==0);
            scope_update(&state,tone,2,1,.017f);
            signal_trace(r,layout.crt,&settings,&state,tone,2,.017f);
            assert(SDL_SetRenderTarget(r,NULL)==0);
            status_lights(r,layout,&state);
        }
        SDL_DestroyTexture(face); SDL_DestroyTexture(trail);
    }
    SDL_DestroyRenderer(r); SDL_DestroyWindow(window); SDL_Quit();
}
int main(void) {
    settings_checks(); tiny_layout_checks(); rendering_checks(); event_checks();
    puts("Instrument settings, tiny-tile layout, render matrix, resize, signal containment and live SDL events passed");
    return 0;
}
