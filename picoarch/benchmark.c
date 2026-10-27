/* SPDX-License-Identifier: MPL-2.0
 * Single-thread, unthrottled libretro frame-time measurement. No game source,
 * other emulator implementation, display driver or audio queue is involved. */
#define _POSIX_C_SOURCE 200809L
#include "libretro.h"
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Failed: %s\n",#x); exit(1); } } while(0)
#define LOAD(name) __typeof__(&name) p_##name=(__typeof__(&name))dlsym(handle,#name); CHECK(p_##name)
static const char* mode;
static unsigned pictures;
static size_t samples;
static bool env(unsigned cmd,void* data) {
    switch(cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(enum retro_pixel_format*)data==RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable* v=data;
        if(!strcmp(v->key,"hcgba_cpu")) v->value=mode;
        else if(!strcmp(v->key,"hcgba_skip_bios")) v->value="enabled";
        else return false;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data=false; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
        const char* dir=getenv("HCGBA_BIOS_DIR");
        if(!dir) return false;
        *(const char**)data=dir; return true;
    }
    default: return false;
    }
}
static void video(const void* data,unsigned w,unsigned h,size_t pitch) {
    CHECK(data && w==240 && h==160 && pitch==480); ++pictures;
}
static size_t audio(const int16_t* data,size_t count) { CHECK(data); samples+=count; return count; }
static void poll(void) {}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned key) {
    (void)p; (void)d; (void)i; (void)key; return 0;
}
static double now(void) {
    struct timespec ts; CHECK(clock_gettime(CLOCK_MONOTONIC,&ts)==0);
    return (double)ts.tv_sec+(double)ts.tv_nsec*1e-9;
}
static unsigned number(const char* s) {
    char* end; errno=0; unsigned long n=strtoul(s,&end,10);
    CHECK(!errno && *s && !*end && n>0 && n<=1000000); return (unsigned)n;
}
static int compare(const void* a,const void* b) {
    double x=*(const double*)a,y=*(const double*)b; return (x>y)-(x<y);
}
int main(int argc,char** argv) {
    if(argc<3 || argc>7) {
        fprintf(stderr,"Usage: %s CORE ROM|--synthetic [frames=3600] [cached|exact] [warmup=300] [state]\n",argv[0]);
        return 2;
    }
    unsigned frames=argc>3?number(argv[3]):3600;
    mode=argc>4?argv[4]:"cached";
    CHECK(!strcmp(mode,"cached") || !strcmp(mode,"exact"));
    unsigned warmup=argc>5?number(argv[5]):300;
    void* handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!handle) { fprintf(stderr,"%s\n",dlerror()); return 1; }
    LOAD(retro_set_environment); LOAD(retro_set_video_refresh); LOAD(retro_set_audio_sample_batch);
    LOAD(retro_set_input_poll); LOAD(retro_set_input_state); LOAD(retro_init); LOAD(retro_deinit);
    LOAD(retro_load_game); LOAD(retro_unload_game); LOAD(retro_run); LOAD(retro_get_system_av_info);
    LOAD(retro_unserialize); LOAD(retro_serialize_size);
    p_retro_set_environment(env); p_retro_set_video_refresh(video); p_retro_set_audio_sample_batch(audio);
    p_retro_set_input_poll(poll); p_retro_set_input_state(input); p_retro_init();
    // Own ARM arithmetic loop: ADD, EOR, SUB, ORR, branch; no idle-loop shortcut.
    uint32_t rom[256]={0xe2800001,0xe0211000,0xe2402007,0xe1823001,0xeafffffa};
    bool synthetic=!strcmp(argv[2],"--synthetic");
    struct retro_game_info game={synthetic?NULL:argv[2],synthetic?rom:NULL,synthetic?sizeof(rom):0,NULL};
    CHECK(p_retro_load_game(&game));
    if(argc>6) {
        size_t size=p_retro_serialize_size(); CHECK(size);
        void* state=malloc(size); CHECK(state);
        FILE* f=fopen(argv[6],"rb"); CHECK(f);
        CHECK(fread(state,1,size,f)==size && fgetc(f)==EOF && !ferror(f)); fclose(f);
        CHECK(p_retro_unserialize(state,size)); free(state);
    }
    struct retro_system_av_info av; p_retro_get_system_av_info(&av);
    CHECK(isfinite(av.timing.fps) && av.timing.fps>0);
    for(unsigned i=0;i<warmup;++i) p_retro_run();
    double* timings=malloc(frames*sizeof(*timings)); CHECK(timings);
    pictures=0; samples=0;
    double total=0, budget=1.0/av.timing.fps;
    unsigned late=0;
    for(unsigned i=0;i<frames;++i) {
        double start=now(); p_retro_run(); double elapsed=now()-start;
        timings[i]=elapsed; total+=elapsed; late+=elapsed>budget;
    }
    CHECK(pictures==frames && samples && total>0);
    qsort(timings,frames,sizeof(*timings),compare);
    // Nearest-rank percentiles; samples are frame costs, not frontend FPS.
    unsigned p95=(unsigned)(((uint64_t)frames*95+99)/100)-1;
    unsigned p99=(unsigned)(((uint64_t)frames*99+99)/100)-1;
    printf("workload=%s mode=%s frames=%u warmup=%u\n",synthetic?"synthetic-ALU":argv[2],mode,frames,warmup);
    printf("target_fps=%.6f budget_ms=%.3f core_fps=%.3f speed=%.3fx\n",
           av.timing.fps,budget*1000,frames/total,frames/total/av.timing.fps);
    printf("mean_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f over_budget=%u/%u headroom=%.1f%%\n",
           total/frames*1000,timings[p95]*1000,timings[p99]*1000,timings[frames-1]*1000,
           late,frames,100*(1-total/frames/budget));
    puts("Core execution only; display/audio output overhead excluded. No input is replayed.");
    free(timings); p_retro_unload_game(); p_retro_deinit(); dlclose(handle);
    return 0;
}
