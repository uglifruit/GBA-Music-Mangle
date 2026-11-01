// Heuristic cartridge candidates for the trial UI; bank association is deliberately manual.
#ifndef KR_DISCOVERY_H
#define KR_DISCOVERY_H
#include "krawall.h"
#define KR_DISCOVERY_MODULES 64
#define KR_DISCOVERY_BANKS 16
typedef struct {uint32_t address;uint16_t count;} KrBank;
typedef struct {
    uint32_t modules[KR_DISCOVERY_MODULES];uint8_t formats[KR_DISCOVERY_MODULES];
    KrBank samples[KR_DISCOVERY_BANKS],instruments[KR_DISCOVERY_BANKS];
    unsigned module_count,sample_count,instrument_count;
} KrDiscovery;
// Zero the result before the first slice. begin/end must be within the supplied ROM.
void kr_discover_slice(KrDiscovery *,const KrRom *,uint32_t begin,uint32_t end);
#endif
