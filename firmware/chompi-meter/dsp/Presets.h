#pragma once
// Built-in mallet presets, chosen by listening in the playground (Oct 26, 2026).
// Material: wood 0, marimba 1/6, vibraphone 2/6, bronze 3/6, glass 4/6,
// bell 5/6, gong 1.

#include "Mallet.h"

namespace meter {

struct MalletPreset {
    const char* name;
    MalletParams p;
    float gain_db; // evens out loudness: measured over the first 0.2 s, C3 .. G5
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
};
static constexpr int kNumMalletPresets = sizeof(kMalletPresets) / sizeof(kMalletPresets[0]);

} // namespace meter
