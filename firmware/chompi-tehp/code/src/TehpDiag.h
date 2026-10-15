#pragma once
/** test builds (make DIAG=1): where the audio callback's time goes, counted in
 *  core cycles (chompi_main.cpp shows it on the white keys) */
#ifdef TEHP_DIAG
#include <cstdint>
enum
{
    kTehpUi,
    kTehpVoices, // all of it, the voice parts below included
    kTehpPush,   // voices: shifter input
    kTehpOut,    // voices: shifter output, the search included
    kTehpSearch, // voices: splice search
    kTehpHead,   // voices: envelope, gain/pan slews, before the source moves
    kTehpStep,   // voices: moving the source on (the shifter input included)
    kTehpTail,   // voices: end of sample, loop points, SD read requests
    kTehpLooper,
    kTehpFx,
    kTehpParts
};
extern uint32_t tehp_ticks[kTehpParts];
extern bool     tehp_in_voices;
#define TEHP_CYCLES (*(volatile uint32_t *)0xE0001004) // DWT->CYCCNT
#define TEHP_TIME_START const uint32_t tehp_t0 = TEHP_CYCLES;
#define TEHP_TIME_END(part) tehp_ticks[part] += TEHP_CYCLES - tehp_t0;
#define TEHP_MARK(t) const uint32_t t = TEHP_CYCLES;
#define TEHP_ADD(t, part) tehp_ticks[part] += TEHP_CYCLES - t;
#else
#define TEHP_MARK(t)
#define TEHP_ADD(t, part)
#define TEHP_TIME_START
#define TEHP_TIME_END(part)
#endif
