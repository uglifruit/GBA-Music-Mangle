// Host-only root scanner and WAV player. The runtime library itself is freestanding.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "mixer_adapter.h"
_Alignas(4) uint8_t g_hostIo[0x400];
static MsPlayer player;
static void put16(FILE *f,unsigned v){fputc(v&255,f);fputc((v>>8)&255,f);}
static void put32(FILE *f,uint32_t v){put16(f,v);put16(f,v>>16);}
static int number(const char *s,uint32_t *v){char *end;unsigned long n=strtoul(s,&end,0);if(!*s||*end||n>UINT32_MAX)return 0;*v=(uint32_t)n;return 1;}
static void header(FILE *f,unsigned frames){fwrite("RIFF",1,4,f);put32(f,36+frames*4);fwrite("WAVEfmt ",1,8,f);put32(f,16);put16(f,1);put16(f,2);
    put32(f,16384);put32(f,65536);put16(f,4);put16(f,16);fwrite("data",1,4,f);put32(f,frames*4);}
int main(int argc,char **argv){
    if(argc<3){fprintf(stderr,"%s scan ROM\n%s render ROM ROOT SONG_INDEX OUT.wav SECONDS [strict]\n%s sample ROM ROOT SAMPLE_INDEX OUT.wav SECONDS\n",argv[0],argv[0],argv[0]);return 2;}
    int scan=!strcmp(argv[1],"scan"),sample=!strcmp(argv[1],"sample");
    if(!scan&&((!sample&&strcmp(argv[1],"render"))||argc<7||argc>8))return 2;
    FILE *f=fopen(argv[2],"rb");if(!f){perror(argv[2]);return 1;}
    if(fseek(f,0,SEEK_END)){fclose(f);return 1;}long length=ftell(f);
    if(length<1||length>0x02000000){fclose(f);return 1;}rewind(f);
    uint8_t *data=malloc((size_t)length);if(!data){fclose(f);return 1;}
    if(fread(data,1,(size_t)length,f)!=(size_t)length){free(data);fclose(f);return 1;}fclose(f);
    MsRom rom={data,0x08000000,(uint32_t)length};MsHooks hooks;ms_mixer_hooks(&hooks,&player);ms_pcm_init();ms_init(&player,rom,&hooks);
    if(scan){uint32_t roots[32];unsigned n=ms_scan(&rom,rom.base,rom.base+rom.size,roots,32);
        for(unsigned i=0;i<n;++i){MsSong song;if(ms_song_info(&rom,roots[i],0,&song)&&ms_start(&player,&song))
            printf("root 0x%08X songs %u samples %u macros %u\n",roots[i],player.song_count,player.sample_count,player.macro_count);}
        free(data);return n?0:1;}
    uint32_t root,index,seconds;
    if(!number(argv[3],&root)||!number(argv[4],&index)||!number(argv[6],&seconds)||!seconds||seconds>600){free(data);return 2;}
    MsSong song;player.preview=argc==8?0:1;
    int started=sample?ms_audition_sample(&player,root,index):(ms_song_info(&rom,root,index,&song)&&ms_start(&player,&song));
    if(!started){fprintf(stderr,"Invalid root/song/sample\n");free(data);return 1;}
    f=fopen(argv[5],"wb");if(!f){perror(argv[5]);free(data);return 1;}
    unsigned frames=seconds*16384;header(f,frames);uint64_t acc=0;if(!sample)ms_tick(&player);
    for(unsigned frame=0;frame<frames;frame+=16){int8_t left[16],right[16];ms_pcm_render(left,right);
        for(unsigned i=0;i<16;++i){put16(f,(uint16_t)((int16_t)left[i]*256));put16(f,(uint16_t)((int16_t)right[i]*256));}
        if(!sample){acc+=(uint64_t)ms_tick_hz_q16(&player)*16;
            while(acc>=((uint64_t)16384<<16)){acc-=((uint64_t)16384<<16);ms_tick(&player);}}
        ms_update(&player);
    }
    int failed=ferror(f)||player.error;if(fclose(f))failed=1;
    fprintf(stderr,"ticks=%u loops=%u unsupported=%08X:%08X warnings=%08X rejected=%u last_opcode=%u error=%u mode=%s\n",player.ticks,player.loops,
            player.unsupported[1],player.unsupported[0],player.warnings,player.rejected,player.last_opcode,player.error,player.preview?"preview":"strict");
    free(data);return failed?1:0;
}
