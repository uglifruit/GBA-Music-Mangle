// Heuristic cartridge candidates for the trial UI; bank association is deliberately manual.
//
// Banks are found as UnkrawerterGBA finds them (MCJack123, MIT): a list is a run of words that all
// point into the image, and its kind is judged from its first four entries only. A real bank can
// hold entries a strict per-entry test refuses (an instrument with no pan envelope stores its point
// count as 255, a junk high byte in a sample map), so the list's extent is never decided by them.
#ifndef KR_DISCOVERY_H
#define KR_DISCOVERY_H
#include "krawall.h"
#define KR_DISCOVERY_MODULES 64
#define KR_DISCOVERY_BANKS 16
#define KR_LIST_MIN 4              // shorter pointer runs are ignored (UnkrawerterGBA's default)
#define KR_LIST_MAX 4096
typedef struct {uint32_t address;uint16_t count;} KrBank;
typedef struct {
    uint32_t modules[KR_DISCOVERY_MODULES];uint8_t formats[KR_DISCOVERY_MODULES];
    KrBank samples[KR_DISCOVERY_BANKS],instruments[KR_DISCOVERY_BANKS];
    unsigned module_count,sample_count,instrument_count;
    uint32_t run_start;unsigned run_count;   // the pointer run being read: it may cross a slice
    unsigned lists;                         // pointer runs judged (diagnostics)
} KrDiscovery;
// Zero the result before the first slice. begin/end must be within the supplied ROM; slices must be
// given in ascending, contiguous order. A run still open at the image's end is judged then.
void kr_discover_slice(KrDiscovery *,const KrRom *,uint32_t begin,uint32_t end);
// The pieces the slice is made of, for a caller with its own (faster) word loop:
int kr_discover_ptr(const KrRom *,uint32_t value);             // could this word be a list entry?
void kr_discover_module(KrDiscovery *,const KrRom *,uint32_t a); // a module at a, in either format
void kr_discover_list(KrDiscovery *,const KrRom *,uint32_t start,unsigned count);
#endif
