#pragma once
// Built-in mallet presets, chosen by listening in the playground (Oct 26, 2026).
// Material: wood 0, marimba 1/6, vibraphone 2/6, bronze 3/6, glass 4/6,
// bell 5/6, gong 1.

#include "Mallet.h"

namespace meter {

// engine: 0 the mallet voice; 1-12 an instrument from the sound study
// (study/instruments.h, in Engine order); 13-20 one of the second set's
// electronic ones (study/synths.h). Their six knobs are the mallet's:
// strike, brightness, decay | shimmer, variation (feel), buzz (prep).
enum Engine { kEngMallet, kEngGangsa, kEngMbira, kEngAmadinda, kEngSiku, kEngKora, kEngVoices, kEngHands, kEngBowed, kEngBowl, kEngPiano, kEngHang, kEngTape,
              kEngKick, kEngTom, kEngNoise, kEngMetal, kEngChip, kEngAcid, kEngFm, kEngGrit, kEngEngines };

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
    // That reads a low, round note louder than it sounds: the hang and the bowl
    // are raised to where the others are on A-weighted single notes, C3 .. E5.
    //                     brightness decay strike  feel  shimmer prep
    {"Amadinda",     {0.f, 0.50f, 0.40f, 0.70f, 0.35f, 0.10f, 0.30f, 0}, -8.7f, kEngAmadinda},
    {"Gangsa",       {0.f, 0.55f, 0.60f, 0.70f, 0.30f, 0.75f, 0.30f, 0}, -12.7f, kEngGangsa},
    {"Mbira",        {0.f, 0.50f, 0.55f, 0.55f, 0.30f, 0.00f, 0.00f, 0}, -19.8f, kEngMbira}, // no caps: CHOMPI + K3 brings them
    {"Kora",         {0.f, 0.55f, 0.50f, 0.55f, 0.30f, 0.30f, 0.00f, 0}, -9.1f, kEngKora},   // no plate: CHOMPI + K3 brings it
    {"Hang",         {0.f, 0.50f, 0.55f, 0.40f, 0.25f, 0.30f, 0.00f, 0}, -0.2f, kEngHang}, // raised 7 dB, see above
    {"Singing bowl", {0.f, 0.50f, 0.25f, 0.45f, 0.20f, 0.35f, 0.00f, 0}, -9.8f, kEngBowl}, // raised 5 dB
    {"Bowed glass",  {0.f, 0.45f, 0.50f, 0.25f, 0.30f, 0.30f, 0.00f, 0}, -33.3f, kEngBowed},
    {"Voices",       {0.f, 0.50f, 0.25f, 0.50f, 0.35f, 0.30f, 0.40f, 0}, -17.3f, kEngVoices},
    {"Siku",         {0.f, 0.45f, 0.30f, 0.60f, 0.35f, 0.30f, 0.15f, 0}, -9.8f, kEngSiku},
    {"Pulse piano",  {0.f, 0.50f, 0.50f, 0.50f, 0.25f, 0.10f, 0.00f, 0}, +21.9f, kEngPiano},
    {"Hands",        {0.f, 0.50f, 0.30f, 0.50f, 0.40f, 0.30f, 0.20f, 0}, +5.4f, kEngHands},
    {"Tape",         {0.f, 0.40f, 0.50f, 0.30f, 0.40f, 0.30f, 0.30f, 0}, -7.0f, kEngTape},
    // The second set, electronic (study/synths.h), each at its setting on the
    // listening page (Nov 7, 2026); the gain as above, at C4. The short ones
    // (hi-hat, clap, snare) come out loud on their peaks: the limiter has them.
    //                     brightness decay strike  feel  shimmer prep
    {"Kick",         {0.f, 0.30f, 0.35f, 0.55f, 0.20f, 0.00f, 0.15f, 0}, -6.8f, kEngKick},
    {"Tom",          {0.f, 0.20f, 0.35f, 0.45f, 0.25f, 0.45f, 0.10f, 0}, -6.5f, kEngTom},
    {"Snare",        {0.f, 0.45f, 0.25f, 0.60f, 0.30f, 0.10f, 0.00f, 0}, +4.0f, kEngNoise},
    {"Clap",         {0.f, 0.35f, 0.35f, 0.30f, 0.35f, 0.10f, 0.85f, 0}, +6.8f, kEngNoise},
    {"Hi-hat",       {0.f, 0.85f, 0.08f, 0.50f, 0.30f, 1.00f, 0.00f, 0}, +11.8f, kEngMetal},
    {"Cowbell",      {0.f, 0.35f, 0.25f, 0.30f, 0.25f, 0.00f, 0.00f, 0}, +3.6f, kEngMetal},
    {"Blip",         {0.f, 0.50f, 0.12f, 0.60f, 0.20f, 0.00f, 0.00f, 0}, +1.8f, kEngChip},
    {"Acid",         {0.f, 0.25f, 0.30f, 0.70f, 0.30f, 0.00f, 0.75f, 0}, +1.8f, kEngAcid},
    {"FM piano",     {0.f, 0.25f, 0.50f, 0.55f, 0.30f, 0.40f, 0.20f, 0}, -4.9f, kEngFm},
    {"FM bell",      {0.f, 0.30f, 0.75f, 0.60f, 0.25f, 0.30f, 0.55f, 0}, -11.6f, kEngFm},
    {"Chip lead",    {0.f, 0.45f, 0.45f, 0.15f, 0.25f, 0.60f, 0.00f, 0}, -5.6f, kEngChip},
    {"Saw stab",     {0.f, 0.55f, 0.35f, 0.40f, 0.30f, 0.80f, 0.20f, 0}, +2.0f, kEngAcid},
    {"Grit",         {0.f, 0.55f, 0.35f, 0.35f, 0.30f, 0.40f, 0.30f, 0}, +1.1f, kEngGrit},
    {"FM bass",      {0.f, 0.35f, 0.30f, 0.50f, 0.25f, 0.00f, 0.05f, 0}, -6.1f, kEngFm},
};
static constexpr int kNumMalletPresets = sizeof(kMalletPresets) / sizeof(kMalletPresets[0]);
static constexpr int kNumClassicPresets = 10; // today's ten, all on the mallet voice

// The instruments on the white keys, a set at a time (with more than one, chosen
// on the lowest black keys in the instrument view): the ensemble from the sound
// study, then the electronic one. Today's ten stay in the firmware (a page's
// lesson can still set them up), not on the keys. -1: nothing on that key.
static constexpr int kSets = 2;
static const int8_t kSetPresets[kSets][15] = {
    // Chosen by CPU (BENCH, cycles per note): no siku, bowed glass, tape or
    // voices, which sound on and cost twice as much, and glass in place of the
    // pulse piano, the dearest of the rest. Keys 1-8 short, for rhythm: wood,
    // marimba, amadinda, gangsa, mbira, kora, snare bar, muffled; 9-14 long,
    // for melodies: vibes, bell, hang, singing bowl, gong, glass.
    {0, 1, 10, 11, 12, 13, 8, 9, 2, 5, 14, 15, 6, 4, -1},
    // The drum machine on 1-8, for rhythm: kick, tom, snare, clap, hi-hat,
    // cowbell, blip, acid; 9-14 for melodies: FM piano, FM bell, chip lead,
    // saw stab, grit, FM bass.
    {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1},
};

} // namespace meter
