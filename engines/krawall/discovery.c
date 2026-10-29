// Deliberately conservative pointer-table heuristics; never claim bank association is certain.
#include "discovery.h"
static int ok(const KrRom *r,uint32_t a,unsigned n){return a>=r->base&&a-r->base<=r->size&&n<=r->size-(a-r->base);}
static unsigned byte(const KrRom *r,uint32_t a){return r->data[a-r->base];}
static unsigned half(const KrRom *r,uint32_t a){return byte(r,a)|(byte(r,a+1)<<8);}
static uint32_t word(const KrRom *r,uint32_t a){return half(r,a)|((uint32_t)half(r,a+2)<<16);}
static int sample(const KrRom *r,uint32_t a){
    if(!ok(r,a,18))return 0;
    uint32_t e=word(r,a+4),length=word(r,a);
    return e>a+18&&ok(r,a+18,e-a-18+1)&&length<=e-a-18&&word(r,a+8)>0&&
        word(r,a+8)<=0xFFFFF&&byte(r,a+14)<=64&&
        (int8_t)byte(r,a+15)>=-64&&(int8_t)byte(r,a+15)<=64&&byte(r,a+16)<=1&&byte(r,a+17)<=1;
}
static int instrument(const KrRom *r,uint32_t a){
    if(!ok(r,a,302))return 0;
    for(unsigned i=0;i<96;++i)if(half(r,a+2*i)>4095)return 0;
    for(unsigned e=192;e<=244;e+=52){
        unsigned max=byte(r,a+e+48);
        if(max>11||byte(r,a+e+49)>max||byte(r,a+e+50)>max||(byte(r,a+e+51)&~7u))return 0;
    }
    return 1;
}
static int known(const KrDiscovery *d,uint32_t a){
    for(unsigned i=0;i<d->sample_count;++i)if(a>=d->samples[i].address&&a-d->samples[i].address<4u*d->samples[i].count)return 1;
    for(unsigned i=0;i<d->instrument_count;++i)if(a>=d->instruments[i].address&&a-d->instruments[i].address<4u*d->instruments[i].count)return 1;
    return 0;
}
void kr_discover_slice(KrDiscovery *d,const KrRom *r,uint32_t begin,uint32_t end){
    if(end<begin||!ok(r,begin,end-begin))return;
    for(uint32_t a=begin;a<end&&end-a>=4;a+=4){
        if(d->module_count<KR_DISCOVERY_MODULES&&kr_module_valid(r,a,KR_MODERN)){
            d->modules[d->module_count]=a;d->formats[d->module_count++]=KR_MODERN;
        }
        if(d->module_count<KR_DISCOVERY_MODULES&&kr_module_valid(r,a,KR_2003)){
            d->modules[d->module_count]=a;d->formats[d->module_count++]=KR_2003;
        }
        if(known(d,a))continue;
        uint32_t v=word(r,a);int kind=sample(r,v)?1:instrument(r,v)?2:0;
        if(!kind)continue;
        unsigned count=1;
        while(count<4096&&ok(r,a,4*count+4)){
            v=word(r,a+4*count);
            if(!(kind==1?sample(r,v):instrument(r,v)))break;
            ++count;
        }
        if(count>=4){
            if(kind==1&&d->sample_count<KR_DISCOVERY_BANKS){unsigned n=d->sample_count++;d->samples[n].address=a;d->samples[n].count=count;}
            if(kind==2&&d->instrument_count<KR_DISCOVERY_BANKS){unsigned n=d->instrument_count++;d->instruments[n].address=a;d->instruments[n].count=count;}
        }
    }
}
