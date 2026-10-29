// Host-only scanner/WAV proof of concept. The freestanding engine never uses stdio or malloc.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "mangle_adapter.h"
#include "discovery.h"
#include "pcm_mixer.h"
_Alignas(4) uint8_t g_hostIo[0x400];
static KrPlayer player;
static void put16(FILE *f,unsigned v){fputc(v&255,f);fputc((v>>8)&255,f);}
static void put32(FILE *f,uint32_t v){put16(f,v);put16(f,v>>16);}
static int number(const char *s,uint32_t *v){char *end;unsigned long n=strtoul(s,&end,0);if(!*s||*end||n>UINT32_MAX)return 0;*v=(uint32_t)n;return 1;}
static void header(FILE *f,unsigned frames){
    fwrite("RIFF",1,4,f);put32(f,36+frames*4);fwrite("WAVEfmt ",1,8,f);put32(f,16);
    put16(f,1);put16(f,2);put32(f,16384);put32(f,65536);put16(f,4);put16(f,16);
    fwrite("data",1,4,f);put32(f,frames*4);
}
int main(int argc,char **argv){
    if(argc<3){fprintf(stderr,"Scan: %s scan ROM [old]\nRender: %s render ROM MODULE SAMPLES SAMPLE_COUNT OUT.wav SECONDS [INSTRUMENTS INSTRUMENT_COUNT [old]]\nAddresses are GBA bus addresses (0x08xxxxxx).\n",argv[0],argv[0]);return 2;}
    int discover=!strcmp(argv[1],"discover");
    int scan=discover||!strcmp(argv[1],"scan");
    if(!scan&&(strcmp(argv[1],"render")||argc<8||argc>11||argc==9))return 2;
    FILE *f=fopen(argv[2],"rb");if(!f){perror(argv[2]);return 1;}
    if(fseek(f,0,SEEK_END)){fclose(f);return 1;}
    long length=ftell(f);if(length<1||length>0x02000000){fclose(f);return 1;}
    rewind(f);uint8_t *data=malloc((size_t)length);if(!data){fclose(f);return 1;}
    if(fread(data,1,(size_t)length,f)!=(size_t)length){free(data);fclose(f);return 1;}fclose(f);
    KrRom rom={data,0x08000000,(uint32_t)length};
    if(discover){
        KrDiscovery d={0};
        for(uint32_t off=0;off<rom.size;off+=0x4000){uint32_t n=rom.size-off;if(n>0x4000)n=0x4000;kr_discover_slice(&d,&rom,rom.base+off,rom.base+off+n);}
        for(unsigned i=0;i<d.module_count;++i)printf("module 0x%08X %s\n",d.modules[i],d.formats[i]?"2003":"modern");
        for(unsigned i=0;i<d.sample_count;++i)printf("samples 0x%08X count %u\n",d.samples[i].address,d.samples[i].count);
        for(unsigned i=0;i<d.instrument_count;++i)printf("instruments 0x%08X count %u\n",d.instruments[i].address,d.instruments[i].count);
        free(data);return d.module_count&&d.sample_count?0:1;
    }
    if(scan){
        uint32_t modules[64];unsigned n=kr_scan(&rom,rom.base,rom.base+rom.size,argc>3?KR_2003:KR_MODERN,modules,64);
        for(unsigned i=0;i<n;++i)printf("0x%08X\n",modules[i]);
        fprintf(stderr,"%u structural candidates; sample/instrument banks must be supplied separately\n",n);
        free(data);return n?0:1;
    }
    KrSong song={0};uint32_t samples,seconds,instruments=0;
    if(!number(argv[3],&song.module)||!number(argv[4],&song.samples)||!number(argv[5],&samples)||samples>65535||
       !number(argv[7],&seconds)||seconds<1||seconds>600){free(data);return 2;}
    song.sample_count=(uint16_t)samples;
    if(argc>=10){if(!number(argv[8],&song.instruments)||!number(argv[9],&instruments)||instruments>65535){free(data);return 2;}}
    song.instrument_count=(uint16_t)instruments;song.format=argc==11?KR_2003:KR_MODERN;
    KrHooks hooks;kr_mixer_hooks(&hooks,&player);kr_pcm_init();kr_init(&player,rom,&hooks);player.repeat=1;
    if(!kr_start(&player,&song)){fprintf(stderr,"Invalid module/bank configuration\n");free(data);return 1;}
    f=fopen(argv[6],"wb");if(!f){perror(argv[6]);free(data);return 1;}
    unsigned frames=seconds*16384;header(f,frames);uint64_t acc=0;
    kr_tick(&player); // tick zero coincides with the start of audio
    for(unsigned frame=0;frame<frames;frame+=16){
        int8_t left[16],right[16];kr_pcm_render(left,right);
        for(unsigned i=0;i<16;++i){put16(f,(uint16_t)((int16_t)left[i]*256));put16(f,(uint16_t)((int16_t)right[i]*256));}
        acc+=(uint64_t)kr_tick_hz_q16(&player)*16;
        while(acc>=((uint64_t)16384<<16)){acc-=((uint64_t)16384<<16);kr_tick(&player);}
    }
    int failed=ferror(f)||player.error;if(fclose(f))failed=1;
    fprintf(stderr,"ticks=%u loops=%u unsupported=%08X:%08X error=%u\n",player.ticks,player.loops,player.unsupported[1],player.unsupported[0],player.error);
    free(data);return failed?1:0;
}
