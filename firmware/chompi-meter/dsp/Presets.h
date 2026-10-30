#pragma once
// Built-in mallet presets, chosen by listening in the playground (Oct 26, 2026).
// Material: wood 0, marimba 1/6, vibraphone 2/6, bronze 3/6, glass 4/6,
// bell 5/6, gong 1.

#include "Mallet.h"

namespace meter {

// engine: 0 the mallet voice; 1-12 an instrument from the sound study
// (study/instruments.h, in Engine order), whose six knobs are the mallet's:
// strike, brightness, decay | shimmer, variation (feel), buzz (prep).
enum Engine { kEngMallet, kEngGangsa, kEngMbira, kEngAmadinda, kEngSiku, kEngKora, kEngVoices, kEngHands, kEngBowed, kEngBowl, kEngPiano, kEngHang, kEngTape, kEngEngines };

struct MalletPreset {
    const char* name;
    MalletParams p;
    float gain_db; // evens out loudness: measured over the first 0.2 s, C3 .. G5
    int engine = kEngMallet;
};

//            material brightness decay strike variation shimmer buzz kind  gain (dB)
static const MalletPreset kMalletPresets[] = {
    {"Wood",      {0.000f, 0.50f, 0.32f, 0.60f, 0.30f, 0.00f, 0.0f, 0}, +4.0f}, // good from C3 up
    {"Marimba",   {0.167f, 0.56f, 0.45f, 0.50f, 0.30f, 0.00f, 0.0f, 0}, +3.0f},
    {"Vibes",     {0.333f, 0.50f, 0.75f, 0.50f, 0.30f, 0.00f, 0.0f, 0}, -2.5f},
    {"Bronze",    {0.500f, 0.50f, 0.72f, 0.65f, 0.35f, 0.70f, 0.0f, 0}, -2.5f},
    {"Glass",     {0.667f, 0.60f, 0.65f, 0.75f, 0.30f, 0.00f, 0.0f, 0}, -1.0f},
    // A slow shimmer: real bells are never quite round, so partials beat.
    {"Bell",      {0.833f, 0.50f, 0.80f, 0.60f, 0.30f, 0.15f, 0.0f, 0}, -6.0f},
    {"Gong",      {1.000f, 0.45f, 0.92f, 0.25f, 0.20f, 0.40f, 0.0f, 0}, -4.0f}, // best low (E2) and high
    {"Felt",      {0.180f, 0.30f, 0.75f, 0.05f, 0.35f, 0.00f, 0.0f, 0}, +0.0f},
    {"Snare bar", {0.000f, 0.55f, 0.28f, 0.65f, 0.45f, 0.00f, 0.3f, 0}, +5.0f}, // rattle
    {"Muffled",   {0.100f, 0.50f, 0.55f, 0.55f, 0.40f, 0.00f, 0.7f, 1}, +0.0f}, // muffle
    // The sound study's instruments, each at its own setting (study/render.cpp
    // Default); the gain matches each one's loudest 0.3 s to the marimba's.
    //                     brightness decay strike  feel  shimmer prep
    {"Amadinda",     {0.f, 0.50f, 0.40f, 0.70f, 0.35f, 0.10f, 0.30f, 0}, -8.7f, kEngAmadinda},
    {"Gangsa",       {0.f, 0.55f, 0.60f, 0.70f, 0.30f, 0.75f, 0.30f, 0}, -12.7f, kEngGangsa},
    {"Mbira",        {0.f, 0.50f, 0.55f, 0.55f, 0.30f, 0.00f, 0.50f, 0}, -19.8f, kEngMbira},
    {"Kora",         {0.f, 0.55f, 0.50f, 0.55f, 0.30f, 0.30f, 0.25f, 0}, -9.1f, kEngKora},
    {"Hang",         {0.f, 0.50f, 0.55f, 0.40f, 0.25f, 0.30f, 0.00f, 0}, -7.2f, kEngHang},
    {"Singing bowl", {0.f, 0.50f, 0.60f, 0.45f, 0.20f, 0.35f, 0.00f, 0}, -14.8f, kEngBowl},
    {"Bowed glass",  {0.f, 0.45f, 0.50f, 0.25f, 0.30f, 0.30f, 0.00f, 0}, -33.3f, kEngBowed},
    {"Voices",       {0.f, 0.50f, 0.25f, 0.50f, 0.35f, 0.30f, 0.40f, 0}, -17.3f, kEngVoices},
    {"Siku",         {0.f, 0.45f, 0.30f, 0.60f, 0.35f, 0.30f, 0.15f, 0}, -9.8f, kEngSiku},
    {"Pulse piano",  {0.f, 0.50f, 0.50f, 0.50f, 0.25f, 0.10f, 0.00f, 0}, +27.9f, kEngPiano},
    {"Hands",        {0.f, 0.50f, 0.30f, 0.50f, 0.40f, 0.30f, 0.20f, 0}, +5.4f, kEngHands},
    {"Tape",         {0.f, 0.40f, 0.50f, 0.30f, 0.40f, 0.30f, 0.30f, 0}, -7.0f, kEngTape},
};
static constexpr int kNumMalletPresets = sizeof(kMalletPresets) / sizeof(kMalletPresets[0]);
static constexpr int kNumClassicPresets = 10; // today's ten, all on the mallet voice

// The instruments on the white keys, a set at a time (with more than one, chosen
// on the black keys in the instrument view). For now one: the ensemble from the
// sound study. Today's ten stay in the firmware (a page's lesson can still set
// them up), the second set to come back once the new ones are chosen:
//     {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1}
// -1: nothing on that key.
static constexpr int kSets = 1;
static const int8_t kSetPresets[kSets][15] = {
    {1, 10, 11, 12, 13, 14, 2, 15, 16, 17, 18, 19, 20, 21, -1},
};

} // namespace meter
