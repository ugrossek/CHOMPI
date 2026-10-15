#pragma once
/** test builds (make DIAG=1): where the audio callback's time goes, counted in
 *  core cycles (chompi_main.cpp shows it on the white keys) */
#ifdef TEHP_DIAG
#include <cstdint>
enum
{
    kTehpUi,
    kTehpVoices, // all of it, the three below included
    kTehpPush,   // voices: shifter input
    kTehpOut,    // voices: shifter output, the search included
    kTehpSearch, // voices: splice search
    kTehpLooper,
    kTehpFx,
    kTehpParts
};
extern uint32_t tehp_ticks[kTehpParts];
extern bool     tehp_in_voices;
#define TEHP_CYCLES (*(volatile uint32_t *)0xE0001004) // DWT->CYCCNT
#define TEHP_TIME_START const uint32_t tehp_t0 = TEHP_CYCLES;
#define TEHP_TIME_END(part) tehp_ticks[part] += TEHP_CYCLES - tehp_t0;
#else
#define TEHP_TIME_START
#define TEHP_TIME_END(part)
#endif
