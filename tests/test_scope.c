#define main retroscope_main
#include "../src/retroscope.c"
#undef main
#include <assert.h>
int main(void) {
    SDL_FPoint p = project(0,0,400,200);
    assert(p.x==200 && p.y==100);
    p=project(.8,.8,400,200);
    assert(p.x==200 && p.y<100); /* mono: vertical */
    p=project(.8,-.8,400,200);
    assert(p.x>200 && p.y==100); /* anti-phase: horizontal */
    p=project(10,-10,400,200);
    assert(p.x==288 && p.y==100); /* clipping + aspect */
    p=project(1,0,200,400);
    assert(p.x==144 && p.y==156); /* left: upper right */
    Audio a = {0};
    float input[2*(CAPACITY+13)];
    for (size_t i=0;i<CAPACITY+13;++i) { input[2*i]=(float)i; input[2*i+1]=-(float)i; }
    buffer_samples(&a,input,CAPACITY+13);
    assert(a.count==CAPACITY && a.head==13);
    assert(a.samples[a.head][0]==13 && a.samples[a.head][1]==-13);
    buffer_samples(&a,NULL,1);
    assert(a.samples[13][0]==0 && a.samples[13][1]==0);
    float invalid[]={NAN,INFINITY};
    buffer_samples(&a,invalid,1);
    assert(a.samples[14][0]==0 && a.samples[14][1]==0);
    puts("Stereo projection and bounded capture buffer tests passed");
    return 0;
}
