#ifndef RETROSCOPE_INSTRUMENT_H
#define RETROSCOPE_INSTRUMENT_H

/* Render-only settings. Capture samples and stream volume are never modified. */
#define GAIN_MIN_STEP (-8)
#define GAIN_MAX_STEP 16
static const float persistence_seconds[] = { .070f, .140f, .300f, .600f };
static const char *const persistence_names[] = { "FAST", "NORMAL", "LONG", "GHOST" };
typedef struct { int gain_step, persistence; } Settings;
#include "scope.h"
typedef struct { SDL_Rect crt; int text_scale, compact; } Layout;

static float visual_gain(const Settings *s) { return powf(2.f, s->gain_step / 4.f); }
static int adjust_setting(Settings *s, SDL_Keycode key) {
    Settings previous = *s;
    if (key == SDLK_PLUS || key == SDLK_EQUALS || key == SDLK_KP_PLUS || key == SDLK_UP) {
        if (s->gain_step < GAIN_MAX_STEP) ++s->gain_step;
    } else if (key == SDLK_MINUS || key == SDLK_KP_MINUS || key == SDLK_DOWN) {
        if (s->gain_step > GAIN_MIN_STEP) --s->gain_step;
    } else if (key == SDLK_0 || key == SDLK_KP_0) s->gain_step = 0;
    else if (key == SDLK_LEFT && s->persistence > 0) --s->persistence;
    else if (key == SDLK_RIGHT && s->persistence < 3) ++s->persistence;
    return previous.gain_step != s->gain_step || previous.persistence != s->persistence;
}
static Uint8 fade_alpha(float elapsed, const Settings *s) {
    return (Uint8)fmaxf(1, fminf(255, 255 * -expm1f(-elapsed / persistence_seconds[s->persistence])));
}
static Layout instrument_layout(int w, int h, int logical_w) {
    int dpi = logical_w > 0 ? (int)lroundf((float)w / logical_w) : 1;
    if (dpi < 1) dpi = 1;
    int compact = w < 400*dpi || h < 260*dpi;
    int margin = (compact ? 4 : 6)*dpi;
    int top = (compact ? 14 : 20)*dpi, bottom = (compact ? 16 : 24)*dpi;
    /* Tiling window managers may assign tiles below the SDL minimum size.
       Keep the CRT at least 1x1 so texture creation cannot fail. */
    int crt_w = w-2*margin > 1 ? w-2*margin : 1;
    int crt_h = h-top-bottom > 1 ? h-top-bottom : 1;
    return (Layout){ {margin, top, crt_w, crt_h}, dpi, compact };
}

/* Original tiny 5x7 bitmap alphabet: no font files or extra library needed. */
static const unsigned char glyphs[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, /* A B */
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30}, /* C D */
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, /* E F */
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, /* G H */
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},       /* I J */
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31}, /* K L */
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, /* M N */
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16}, /* O P */
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, /* Q R */
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},      /* S T */
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4}, /* U V */
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17}, /* W X */
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},     /* Y Z */
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},   /* 0 1 */
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},     /* 2 3 */
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},   /* 4 5 */
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},   /* 6 7 */
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14} /* 8 9 */
};
static void label(SDL_Renderer *r, int x, int y, int scale, const char *text) {
    for (; *text; ++text, x += 7*scale) {
        unsigned char ch = (unsigned char)*text;
        if (ch >= 'a' && ch <= 'z') ch -= 'a'-'A';
        const unsigned char *rows = NULL;
        if (ch >= 'A' && ch <= 'Z') rows = glyphs[ch-'A'];
        else if (ch >= '0' && ch <= '9') rows = glyphs[26+ch-'0'];
        for (int row=0;row<7;++row) for (int col=0;col<5;++col) {
            int on = rows ? !!(rows[row] & (1 << (4-col))) :
                (ch=='.' && row==6 && col==2) ||
                (ch=='/' && col==4-row/2);
            if (on) { SDL_Rect pixel = {x+col*scale,y+row*scale,scale,scale}; SDL_RenderFillRect(r,&pixel); }
        }
    }
}
static void faceplate(SDL_Renderer *r, int w, int h, Layout layout, const Settings *s, ScopeMode mode, unsigned timebase) {
    SDL_Rect crt = layout.crt;
    int unit = layout.text_scale;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 14,15,14,255); SDL_RenderClear(r);
    SDL_Rect rim = {crt.x-2*unit,crt.y-2*unit,crt.w+4*unit,crt.h+4*unit};
    SDL_SetRenderDrawColor(r, 30,34,29,255); SDL_RenderDrawRect(r,&rim);
    SDL_SetRenderDrawColor(r, 2,9,5,255); SDL_RenderFillRect(r,&crt);
    /* Subtle scanlines and edge darkening are static, cached with the grid. */
    SDL_SetRenderDrawColor(r, 0,0,0,35);
    for (int y=crt.y;y<crt.y+crt.h;y+=3*unit) SDL_RenderDrawLine(r,crt.x,y,crt.x+crt.w-1,y);
    for (int i=0;i<6*unit;++i) {
        SDL_Rect edge={crt.x+i,crt.y+i,crt.w-2*i,crt.h-2*i};
        SDL_SetRenderDrawColor(r,0,0,0,(Uint8)(85-i*10/unit)); SDL_RenderDrawRect(r,&edge);
    }
    ScopeGeometry g=scope_geometry(crt,mode);
    float cx=crt.x+g.cx,cy=crt.y+g.cy;
    /* Eight main divisions per axis, five fine subdivisions per division. */
    SDL_SetRenderDrawColor(r,7,20,13,255);
    for(int i=-20;i<=20;++i) if(i%5) {
        float x=i*g.half_x/20, y=i*g.half_y/20;
        if(g.half_x/20>=3*unit) SDL_RenderDrawLineF(r,cx+x,cy-g.half_y,cx+x,cy+g.half_y);
        if(g.half_y/20>=3*unit) SDL_RenderDrawLineF(r,cx-g.half_x,cy+y,cx+g.half_x,cy+y);
    }
    SDL_SetRenderDrawColor(r,13,35,22,255);
    for(int i=-4;i<=4;++i) if(i) {
        float x=i*g.half_x*.25f,y=i*g.half_y*.25f;
        SDL_RenderDrawLineF(r,cx+x,cy-g.half_y,cx+x,cy+g.half_y);
        SDL_RenderDrawLineF(r,cx-g.half_x,cy+y,cx+g.half_x,cy+y);
    }
    SDL_SetRenderDrawColor(r,28,66,40,255);
    SDL_RenderDrawLineF(r,cx,cy-g.half_y,cx,cy+g.half_y);
    SDL_RenderDrawLineF(r,cx-g.half_x,cy,cx+g.half_x,cy);
    for(int i=-20;i<=20;++i) if(i) {
        float tick=(i%5==0 ? 3 : 1)*unit;
        SDL_RenderDrawLineF(r,cx+i*g.half_x/20,cy-tick,cx+i*g.half_x/20,cy+tick);
        SDL_RenderDrawLineF(r,cx-tick,cy+i*g.half_y/20,cx+tick,cy+i*g.half_y/20);
    }
    int top_y=(layout.compact ? 3 : 6)*unit;
    int mode_x=w-crt.x-((int)strlen(mode_names[mode])*7+35)*unit;
    SDL_SetRenderDrawColor(r,113,77,38,255);
    label(r,mode_x,top_y,unit,"MODE");
    label(r,layout.compact ? crt.x : crt.x+56*unit,top_y,unit,layout.compact ? "S" : "SIGNAL");
    label(r,layout.compact ? crt.x+28*unit : crt.x+119*unit,top_y,unit,layout.compact ? "C" : "CLIP");
    SDL_SetRenderDrawColor(r,184,129,62,255);
    if(!layout.compact) label(r,crt.x,top_y,unit,"SCOPE");
    label(r,mode_x+35*unit,top_y,unit,mode_names[mode]);
    char gain[32]; snprintf(gain,sizeof gain,"X%.2f",visual_gain(s));
    if(layout.compact) {
        int y=h-11*unit;
        SDL_SetRenderDrawColor(r,113,77,38,255);
        label(r,crt.x,y,unit,"GAIN");
        const char *persist=persistence_names[s->persistence];
        int px=w-crt.x-((int)strlen(persist)*7+14)*unit;
        label(r,px,y,unit,"P");
        SDL_SetRenderDrawColor(r,184,129,62,255);
        label(r,crt.x+35*unit,y,unit,gain);
        label(r,px+14*unit,y,unit,persist);
    } else {
        int y=h-20*unit;
        int columns[]={crt.x, w/3, w*2/3, w-crt.x-28*unit};
        SDL_SetRenderDrawColor(r,113,77,38,255);
        label(r,columns[0],y,unit,"GAIN"); label(r,columns[1],y,unit,"PERSIST");
        label(r,columns[2],y,unit,mode==SCOPE_WAVE ? "TIME" : "SIGNAL"); label(r,columns[3],y,unit,"RATE");
        SDL_SetRenderDrawColor(r,184,129,62,255);
        label(r,columns[0],y+10*unit,unit,gain);
        label(r,columns[1],y+10*unit,unit,persistence_names[s->persistence]);
        label(r,columns[2],y+10*unit,unit,mode==SCOPE_WAVE ? timebase_names[timebase] : mode==SCOPE_XY ? "L/R XY" : "STEREO");
        label(r,columns[3],y+10*unit,unit,"48K");
    }
}
static void status_lights(SDL_Renderer *r, Layout layout, const ScopeState *state) {
    int unit=layout.text_scale, y=(layout.compact ? 6 : 9)*unit;
    int signal_x=layout.crt.x+(layout.compact ? 14 : 105)*unit;
    int clip_x=layout.crt.x+(layout.compact ? 42 : 154)*unit;
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    for(int light=0;light<2;++light) {
        int lit=light ? state->clip_hold>0 : state->signal_hold>0;
        int x=light ? clip_x : signal_x;
        if(lit) SDL_SetRenderDrawColor(r,light ? 222 : 117,light ? 139 : 192,light ? 52 : 90,255);
        else SDL_SetRenderDrawColor(r,light ? 53 : 25,light ? 36 : 47,light ? 20 : 28,255);
        for(int row=-2*unit;row<=2*unit;++row) {
            int half=(abs(row)>unit ? 1 : 2)*unit;
            SDL_RenderDrawLine(r,x-half,y+row,x+half,y+row);
        }
    }
}
static void signal_trace(SDL_Renderer *r, SDL_Rect crt, const Settings *settings,
                         const ScopeState *state, const float samples[][2], size_t count, float elapsed) {
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,0,0,0,fade_alpha(elapsed,settings)); SDL_RenderFillRect(r,NULL);
    SDL_FPoint points[CAPACITY], normals[CAPACITY];
    size_t n=scope_points(state,samples,count,visual_gain(settings),crt,points);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_ADD);
    if(n>0) {
        for(size_t i=0;i<n;++i) {
            SDL_FPoint a=points[i ? i-1 : i],b=points[i+1<n ? i+1 : i];
            float dx=b.x-a.x,dy=b.y-a.y,length=hypotf(dx,dy);
            normals[i]=length>0 ? (SDL_FPoint){-dy/length,dx/length} : (SDL_FPoint){0,0};
        }
        SDL_SetRenderDrawColor(r,18,140,40,48);
        for(int offset=-1;offset<=1;offset+=2) {
            for(size_t i=0;i<n;++i) { points[i].x+=normals[i].x*offset*1.2f; points[i].y+=normals[i].y*offset*1.2f; }
            if(n>1) SDL_RenderDrawLinesF(r,points,(int)n);
            for(size_t i=0;i<n;++i) { points[i].x-=normals[i].x*offset*1.2f; points[i].y-=normals[i].y*offset*1.2f; }
        }
        SDL_SetRenderDrawColor(r,100,245,135,170);
        if(n>1) SDL_RenderDrawLinesF(r,points,(int)n);
        SDL_RenderDrawPointF(r,points[n-1].x,points[n-1].y);
    } else {
        SDL_SetRenderDrawColor(r,70,175,95,70);
        if(state->mode==SCOPE_WAVE) {
            ScopeGeometry g=scope_geometry(crt,SCOPE_WAVE);
            SDL_RenderDrawLineF(r,g.cx-g.half_x,g.cy,g.cx+g.half_x,g.cy);
        } else SDL_RenderDrawPointF(r,crt.w*.5f,crt.h*.5f);
    }
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
}
static const char *help_text =
    "RetroScope: passiver Stereo-Output-Monitor\n"
    "Q / Esc: Beenden; H: Hilfe\n"
    "1: VECTOR (Stereo Mid/Side); 2: WAVE (L/R-Mittelwert); 3: XY (X=L, Y=R)\n"
    "WAVE: automatischer steigender Nulltrigger; B: Timebase 20 / 5 / 40 ms\n"
    "+ / Pfeil hoch: Gain erhöhen; - / Pfeil runter: Gain reduzieren\n"
    "0: Gain auf 1.00; Bereich 0.25 bis 16.00 (nur Darstellung)\n"
    "Pfeil links / rechts: Persistence kürzer / länger (FAST 70, NORMAL 140, LONG 300, GHOST 600 ms)\n";
#endif
