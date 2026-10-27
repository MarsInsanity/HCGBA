/* SPDX-License-Identifier: MPL-2.0 */
#include "libretro.h"
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
#define LOAD(name) __typeof__(&name) p_##name=(__typeof__(&name))dlsym(handle,#name); CHECK(p_##name)
static unsigned pictures,samples;
static bool wantExact, update;
static uint32_t videoHash;
static bool env(unsigned cmd,void* data) {
    switch(cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format*)data==RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable* v=data;
        v->value=!strcmp(v->key,"hcgba_cpu") ? (wantExact?"exact":"cached") : "enabled";
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data=update; update=false; return true;
    default:return false;
    }
}
static void video(const void* data,unsigned w,unsigned h,size_t pitch) {
    CHECK(data && w==240 && h==160 && pitch==480);
    ++pictures; videoHash=2166136261u;
    const uint8_t* b=data;
    for(unsigned n=0;n<pitch*h;++n) videoHash=(videoHash^b[n])*16777619u;
}
static size_t audio(const int16_t* data,size_t count) { CHECK(data && count<4096); samples+=count; return count; }
static void poll(void) {}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned key) { return key==RETRO_DEVICE_ID_JOYPAD_A; }
int main(int argc,char**argv) {
    CHECK(argc==2);
    void* handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!handle) { fprintf(stderr,"%s\n",dlerror()); return 1; }
    LOAD(retro_set_environment); LOAD(retro_set_video_refresh); LOAD(retro_set_audio_sample_batch);
    LOAD(retro_set_input_poll); LOAD(retro_set_input_state); LOAD(retro_init); LOAD(retro_deinit);
    LOAD(retro_load_game); LOAD(retro_unload_game); LOAD(retro_run); LOAD(retro_reset);
    LOAD(retro_serialize_size); LOAD(retro_serialize); LOAD(retro_unserialize);
    LOAD(retro_get_memory_data); LOAD(retro_get_memory_size); LOAD(retro_get_system_av_info);
    p_retro_set_environment(env); p_retro_set_video_refresh(video); p_retro_set_audio_sample_batch(audio);
    p_retro_set_input_poll(poll); p_retro_set_input_state(input); p_retro_init();
    struct retro_system_av_info av; p_retro_get_system_av_info(&av);
    CHECK(av.timing.sample_rate==32768 && av.timing.fps>59.7 && av.timing.fps<59.8);
    uint32_t rom[256]={0xe3a00402,0xe3a0105a,0xe5801000,0xeafffffe};
    struct retro_game_info game={NULL,rom,sizeof(rom),NULL};
    CHECK(p_retro_load_game(&game));
    CHECK(p_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM)==128*1024);
    uint8_t* save=p_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    CHECK(save); save[123]=0x57;
    for(unsigned n=0;n<3;++n) p_retro_run();
    CHECK(*(uint32_t*)p_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)==0x5a);
    CHECK(pictures==3 && samples>1000 && samples<1700);
    size_t size=p_retro_serialize_size();
    uint8_t* state=malloc(size+1); CHECK(state);
    CHECK(p_retro_serialize(state+1,size)); // deliberately unaligned
    for(unsigned n=0;n<2;++n) p_retro_run();
    uint32_t hash=videoHash;
    CHECK(p_retro_unserialize(state+1,size));
    wantExact=true; update=true;
    for(unsigned n=0;n<2;++n) p_retro_run();
    CHECK(videoHash==hash);
    CHECK(!p_retro_unserialize(state+1,16));
    free(state);
    p_retro_reset(); CHECK(save[123]==0x57);
    p_retro_unload_game();
    CHECK(!p_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
    // Exercise picoarch's need_fullpath load route too.
    char name[]="/tmp/hcgba-test-XXXXXX";
    int fd=mkstemp(name); CHECK(fd>=0);
    CHECK(write(fd,rom,sizeof(rom))==sizeof(rom)); close(fd);
    game=(struct retro_game_info){name,NULL,0,NULL};
    CHECK(p_retro_load_game(&game)); p_retro_run(); unlink(name);
    p_retro_deinit(); dlclose(handle);
    puts("PASS: libretro load, RGB565, 32768 Hz, input API, SRAM, unaligned states, reset, file load");
    return 0;
}
