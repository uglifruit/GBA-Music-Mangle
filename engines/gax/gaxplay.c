// Host-only scanner and WAV audition tool. The firmware core itself has no libc dependency.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "mangle_adapter.h"
_Alignas(4) uint8_t g_hostIo[0x400];
static GxPlayer player;
static void put16(FILE *f,unsigned v){fputc(v&255,f);fputc((v>>8)&255,f);}
static void put32(FILE *f,uint32_t v){put16(f,v);put16(f,v>>16);}
static int number(const char *s,uint32_t *v){char *end;unsigned long n=strtoul(s,&end,0);if(!*s||*end||n>UINT32_MAX)return 0;*v=(uint32_t)n;return 1;}
static void header(FILE *f,unsigned frames){fwrite("RIFF",1,4,f);put32(f,36+frames*4);fwrite("WAVEfmt ",1,8,f);put32(f,16);put16(f,1);put16(f,2);
    put32(f,16384);put32(f,65536);put16(f,4);put16(f,16);fwrite("data",1,4,f);put32(f,frames*4);}
int main(int argc,char **argv){
    if(argc<3){fprintf(stderr,"Scan: %s scan ROM\nRender: %s render ROM SONG_ADDRESS OUT.wav SECONDS\n",argv[0],argv[0]);return 2;}
    int scan=!strcmp(argv[1],"scan");if(!scan&&(strcmp(argv[1],"render")||argc!=6))return 2;
    FILE *f=fopen(argv[2],"rb");if(!f){perror(argv[2]);return 1;}
    if(fseek(f,0,SEEK_END)){fclose(f);return 1;}long length=ftell(f);
    if(length<1||length>0x02000000){fclose(f);return 1;}rewind(f);
    uint8_t *data=malloc((size_t)length);if(!data){fclose(f);return 1;}
    if(fread(data,1,(size_t)length,f)!=(size_t)length){free(data);fclose(f);return 1;}fclose(f);
    GxRom rom={data,0x08000000,(uint32_t)length};
    if(scan){uint32_t songs[64];unsigned n=gx_scan(&rom,rom.base,rom.base+rom.size,songs,64);
        for(unsigned i=0;i<n;++i)printf("0x%08X\n",songs[i]);
        fprintf(stderr,"%u structural GAX 3.05-style song candidates\n",n);free(data);return n?0:1;}
    uint32_t song,seconds;
    if(!number(argv[3],&song)||!number(argv[5],&seconds)||!seconds||seconds>600){free(data);return 2;}
    GxHooks hooks;gx_mixer_hooks(&hooks,&player);gx_pcm_init();gx_init(&player,rom,&hooks);
    if(!gx_start(&player,song)){fprintf(stderr,"Invalid song header\n");free(data);return 1;}
    f=fopen(argv[4],"wb");if(!f){perror(argv[4]);free(data);return 1;}
    unsigned frames=seconds*16384;header(f,frames);uint64_t acc=0;gx_frame(&player);
    for(unsigned frame=0;frame<frames;frame+=16){int8_t left[16],right[16];gx_pcm_render(left,right);
        for(unsigned i=0;i<16;++i){put16(f,(uint16_t)((int16_t)left[i]*256));put16(f,(uint16_t)((int16_t)right[i]*256));}
        acc+=(uint64_t)gx_frame_hz_q16(&player)*16;
        while(acc>=((uint64_t)16384<<16)){acc-=((uint64_t)16384<<16);gx_frame(&player);}
    }
    int failed=ferror(f)||player.error;if(fclose(f))failed=1;
    fprintf(stderr,"frames=%u loops=%u unsupported=%08X:%08X:%08X error=%u\n",player.frames,player.loops,player.unsupported[2],player.unsupported[1],player.unsupported[0],player.error);
    free(data);return failed?1:0;
}
