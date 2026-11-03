// Deliberately conservative pointer-table heuristics; never claim bank association is certain.
// Bank tests follow Krawall's converter (krawerter Sample.cpp / Instrument.cpp) and UnkrawerterGBA's
// searchForOffsets, which has met real carts: see discovery.h.
#include "discovery.h"
static int ok(const KrRom *r,uint32_t a,unsigned n){return a>=r->base&&a-r->base<=r->size&&n<=r->size-(a-r->base);}
static unsigned byte(const KrRom *r,uint32_t a){return r->data[a-r->base];}
static unsigned half(const KrRom *r,uint32_t a){return byte(r,a)|(byte(r,a+1)<<8);}
static uint32_t word(const KrRom *r,uint32_t a){return half(r,a)|((uint32_t)half(r,a+2)<<16);}
// A Sample: loopLength, end pointer, c2Freq, finetune, relnote, volume, pan, loop, hq, data.
// Pan and volume are not judged (older converters may differ); the end pointer is the strong test.
static int sample(const KrRom *r,uint32_t a){
    if(!ok(r,a,18))return 0;
    uint32_t e=word(r,a+4),length=word(r,a);
    return e>a+18&&ok(r,a+18,e-a-18)&&length<=e-a-18&&word(r,a+8)<=0xFFFFF&&
        byte(r,a+16)<=1&&byte(r,a+17)<=1;
}
// An Instrument: a 96-entry sample map, then two envelopes of 12 nodes + max/sus/loop/flags. "max"
// is the point count less one, so an envelope with no points stores 255. A map entry's high byte
// can be junk (UnkrawerterGBA saw it on Cocoto): it passes if its low byte matches a neighbour's.
static int instrument(const KrRom *r,uint32_t a){
    if(!ok(r,a,302))return 0;
    for(unsigned i=0;i<96;++i){
        unsigned v=half(r,a+2*i);
        if(v<=4095)continue;
        unsigned lo=v&255;
        if(!((i>0&&(half(r,a+2*i-2)&255)==lo)||(i<95&&(half(r,a+2*i+2)&255)==lo)))return 0;
    }
    for(unsigned e=192;e<=244;e+=52){
        unsigned max=byte(r,a+e+48);
        if((max>11&&max!=255)||byte(r,a+e+49)>12||byte(r,a+e+50)>12)return 0;
    }
    return 1;
}
int kr_discover_ptr(const KrRom *r,uint32_t v){return v-r->base<r->size&&v!=0x08080808u;}
// One reading per module: the 2004-07-07+ format when it is valid, else the 2003 one. A module valid
// both ways is the newer (an old pattern reads as new only when its first row is empty: in the new
// format the byte after the row count is its high byte; UnkrawerterGBA decides the same way), and
// listing both doubled every song with a misread copy.
void kr_discover_module(KrDiscovery *d,const KrRom *r,uint32_t a){
    if(d->module_count<KR_DISCOVERY_MODULES&&kr_module_valid(r,a,KR_MODERN)){
        d->modules[d->module_count]=a;d->formats[d->module_count++]=KR_MODERN;
        return;
    }
    if(d->module_count<KR_DISCOVERY_MODULES&&kr_module_valid(r,a,KR_2003)){
        d->modules[d->module_count]=a;d->formats[d->module_count++]=KR_2003;
    }
}
// A finished pointer run: a sample bank or an instrument bank if its first four entries are all
// one and not the other, and they ascend at least 16 bytes apart (a bank's entries are laid out in
// order; this turns away runs of a repeated constant and most code tables).
void kr_discover_list(KrDiscovery *d,const KrRom *r,uint32_t start,unsigned count){
    if(count<KR_LIST_MIN||count>=KR_LIST_MAX||!ok(r,start,4u*count))return;
    ++d->lists;
    unsigned n=count<4?count:4;int s=1,in=1;
    for(unsigned i=0;i<n;++i){
        uint32_t v=word(r,start+4*i);
        if(i&&v<word(r,start+4*i-4)+16)return;
        if(s&&!sample(r,v))s=0;
        if(in&&!instrument(r,v))in=0;
        if(!s&&!in)return;
    }
    if(s==in)return;
    if(s&&d->sample_count<KR_DISCOVERY_BANKS){unsigned k=d->sample_count++;d->samples[k].address=start;d->samples[k].count=(uint16_t)count;}
    if(in&&d->instrument_count<KR_DISCOVERY_BANKS){unsigned k=d->instrument_count++;d->instruments[k].address=start;d->instruments[k].count=(uint16_t)count;}
}
void kr_discover_slice(KrDiscovery *d,const KrRom *r,uint32_t begin,uint32_t end){
    if(end<begin||!ok(r,begin,end-begin))return;
    for(uint32_t a=begin;a<end&&end-a>=4;a+=4){
        kr_discover_module(d,r,a);
        if(kr_discover_ptr(r,word(r,a))){if(!d->run_count++)d->run_start=a;continue;}
        if(d->run_count)kr_discover_list(d,r,d->run_start,d->run_count);
        d->run_count=0;
    }
    if(end-r->base>=r->size&&d->run_count){kr_discover_list(d,r,d->run_start,d->run_count);d->run_count=0;}
}
