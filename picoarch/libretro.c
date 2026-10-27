/* SPDX-License-Identifier: MPL-2.0
 * Compact libretro frontend for the GBA machine already present in HCGBA. */
#include "libretro.h"
#include "gba.h"
#include "cpu/jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static struct GBA* gba;
static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_t sample;
static retro_audio_sample_batch_t batch;
static retro_input_poll_t poll;
static retro_input_state_t input;
static retro_log_printf_t logger;
static bool skipBios;
static bool exactCpu;
static bool controller = true;

static void options(void) {
    struct retro_variable v={"hcgba_skip_bios",NULL};
    skipBios=environment && environment(RETRO_ENVIRONMENT_GET_VARIABLE,&v) && v.value && !strcmp(v.value,"enabled");
    v=(struct retro_variable){"hcgba_cpu",NULL};
    exactCpu=environment && environment(RETRO_ENVIRONMENT_GET_VARIABLE,&v) && v.value && !strcmp(v.value,"exact");
}
void retro_set_environment(retro_environment_t cb) {
    environment=cb;
    static const struct retro_variable vars[]={
        {"hcgba_cpu","CPU execution; cached|exact"},
        {"hcgba_skip_bios","Skip BIOS intro (restart); disabled|enabled"},
        {NULL,NULL}
    };
    if (cb) cb(RETRO_ENVIRONMENT_SET_VARIABLES,(void*)vars);
}
void retro_set_video_refresh(retro_video_refresh_t cb) { video=cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { sample=cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { batch=cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll=cb; }
void retro_set_input_state(retro_input_state_t cb) { input=cb; }
void retro_set_controller_port_device(unsigned port,unsigned device) {
    if (!port) controller=(device & RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD;
}
unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_init(void) {
    struct retro_log_callback log={0};
    if (environment && environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE,&log)) logger=log.log;
    options();
}
void retro_unload_game(void) {
    if (gba) gbaDestroy(gba);
    gba=NULL;
}
void retro_deinit(void) { retro_unload_game(); logger=NULL; }
void retro_get_system_info(struct retro_system_info* info) {
    *info=(struct retro_system_info){"HCGBA","0.1-mips32","gba|agb|bin",true,false};
}
void retro_get_system_av_info(struct retro_system_av_info* info) {
    *info=(struct retro_system_av_info){
        {240,160,240,160,1.5f},
        {16777216.0/280896.0,32768.0}
    };
}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
static int64_t clockNow(void* context) { return (int64_t)time(NULL); }

static bool loadBios(void) {
    const char* dir=NULL;
    char name[4096];
    if (environment && environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,&dir) && dir) {
        int n=snprintf(name,sizeof(name),"%s/gba_bios.bin",dir);
        if (n>0 && (size_t)n<sizeof(name)) {
            FILE* f=fopen(name,"rb");
            if (f) {
                uint8_t data[SIZE_BIOS];
                bool ok=fread(data,1,sizeof(data),f)==sizeof(data) && fgetc(f)==EOF;
                fclose(f);
                if (ok) return gbaLoadBios(gba,data,sizeof(data));
                if (logger) logger(RETRO_LOG_WARN,"HCGBA: invalid gba_bios.bin size; using built-in BIOS\n");
            }
        }
    }
    return gbaLoadBios(gba,armgbaBios,SIZE_BIOS);
}
bool retro_load_game(const struct retro_game_info* game) {
    retro_unload_game();
    if (!game || (!game->data && !game->path)) return false;
    enum retro_pixel_format fmt=RETRO_PIXEL_FORMAT_RGB565;
    if (!environment || !environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&fmt)) return false;
    options();
    gba=gbaCreate();
    if (!gba) return false;
    gba->savedata.backed=true;
    gba->renderer.rgb565=true;
    gba->gpio.clock=clockNow;
    bool ok=loadBios();
    if (ok && game->data) {
        ok=game->size>=0xc0 && gbaLoadRom(gba,game->data,game->size);
    } else if (ok && game->path) {
        // File mapping avoids a second malloc-sized copy of a large cartridge.
        int fd=open(game->path,O_RDONLY);
        struct stat st;
        ok=fd>=0 && fstat(fd,&st)==0 && st.st_size>=0xc0 &&
           (uint64_t)st.st_size <= 128u*1024u*1024u;
        if (ok) {
            void* data=mmap(NULL,(size_t)st.st_size,PROT_READ,MAP_PRIVATE,fd,0);
            ok=data!=MAP_FAILED;
            if (ok) {
                ok=gbaLoadRom(gba,data,(size_t)st.st_size);
                munmap(data,(size_t)st.st_size);
            }
        }
        if (fd>=0) close(fd);
    }
    if (!ok) { retro_unload_game(); return false; }
    gbaReset(gba,skipBios);
    static const struct retro_input_descriptor desc[]={
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"A"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"B"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_SELECT,"Select"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Start"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT,"Right"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT,"Left"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP,"Up"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN,"Down"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"R"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"L"},
        {0,0,0,0,NULL}
    };
    environment(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,(void*)desc);
    return true;
}
bool retro_load_game_special(unsigned type,const struct retro_game_info* info,size_t count) { return false; }
void retro_reset(void) { if (gba) { options(); gbaReset(gba,skipBios); } }
void retro_run(void) {
    if (!gba) return;
    bool changed=false;
    if (environment && environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE,&changed) && changed) options();
    if (poll) poll();
    static const unsigned buttons[]={RETRO_DEVICE_ID_JOYPAD_A,RETRO_DEVICE_ID_JOYPAD_B,
        RETRO_DEVICE_ID_JOYPAD_SELECT,RETRO_DEVICE_ID_JOYPAD_START,RETRO_DEVICE_ID_JOYPAD_RIGHT,
        RETRO_DEVICE_ID_JOYPAD_LEFT,RETRO_DEVICE_ID_JOYPAD_UP,RETRO_DEVICE_ID_JOYPAD_DOWN,
        RETRO_DEVICE_ID_JOYPAD_R,RETRO_DEVICE_ID_JOYPAD_L};
    uint16_t keys=0;
    if (controller && input) for (unsigned b=0;b<10;++b)
        if (input(0,RETRO_DEVICE_JOYPAD,0,buttons[b])) keys|=1u<<b;
    gbaSetKeys(gba,keys);
    uint32_t frame=gba->video.frameCounter;
    do {
        if (!exactCpu) gbaRunPart(gba);
        else {
            if (gba->cpu.halted) {
                if (gba->cpu.cycles<gba->cpu.nextEvent) gba->cpu.cycles=gba->cpu.nextEvent;
                processEvents(gba);
            } else stepInstruction(gba);
            gba->returnRequested=false;
        }
    } while (gba->video.frameCounter==frame);
    audioCatchUp(gba,timingCurrentTime(gba),false);
    if (video) video(gba->renderer.frame,240,160,240*sizeof(uint16_t));
    if (batch) batch((const int16_t*)gba->audio.out,gba->audio.outCount);
    else if (sample) for (uint32_t i=0;i<gba->audio.outCount;++i)
        sample(gba->audio.out[i].left,gba->audio.out[i].right);
    gba->audio.outCount=0;
}
size_t retro_serialize_size(void) { return gbaStateSize(); }
bool retro_serialize(void* data,size_t size) {
    if (!gba || !data || size<gbaStateSize()) return false;
    // Frontends may supply a byte-aligned buffer; the state's machine needs 8.
    void* aligned=malloc(gbaStateSize());
    if (!aligned) return false;
    gbaSaveState(gba,aligned);
    memcpy(data,aligned,gbaStateSize());
    free(aligned);
    return true;
}
bool retro_unserialize(const void* data,size_t size) {
    if (!gba || !data || size<gbaStateSize()) return false;
    void* aligned=malloc(gbaStateSize());
    if (!aligned) return false;
    memcpy(aligned,data,gbaStateSize());
    bool ok=gbaLoadState(gba,aligned,gbaStateSize());
    free(aligned);
    return ok;
}
void* retro_get_memory_data(unsigned id) {
    if (!gba) return NULL;
    switch (id) {
    case RETRO_MEMORY_SAVE_RAM: return gba->savedata.storage;
    case RETRO_MEMORY_SYSTEM_RAM: return gba->ewram;
    case RETRO_MEMORY_VIDEO_RAM: return gba->vram;
    default: return NULL;
    }
}
size_t retro_get_memory_size(unsigned id) {
    if (!gba) return 0;
    switch (id) {
    // Stable from load time: picoarch asks before autodetection can run.
    case RETRO_MEMORY_SAVE_RAM: return sizeof(gba->savedata.storage);
    case RETRO_MEMORY_SYSTEM_RAM: return sizeof(gba->ewram);
    case RETRO_MEMORY_VIDEO_RAM: return sizeof(gba->vram);
    default: return 0;
    }
}
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index,bool enabled,const char* code) {}
