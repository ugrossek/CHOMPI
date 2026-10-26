#pragma once
// Built-in mallet presets, chosen by listening in the playground (Oct 26, 2026).
// Material: wood 0, marimba 1/6, vibraphone 2/6, bronze 3/6, glass 4/6,
// bell 5/6, gong 1.

#include "Mallet.h"

namespace meter {

struct MalletPreset {
    const char* name;
    MalletParams p;
};

//            material brightness decay strike variation shimmer buzz kind
static const MalletPreset kMalletPresets[] = {
    {"Wood",      {0.000f, 0.50f, 0.32f, 0.60f, 0.30f, 0.00f, 0.0f, 0}}, // good from C3 up
    {"Marimba",   {0.167f, 0.56f, 0.45f, 0.50f, 0.30f, 0.00f, 0.0f, 0}},
    {"Vibes",     {0.333f, 0.50f, 0.75f, 0.50f, 0.30f, 0.00f, 0.0f, 0}},
    {"Bronze",    {0.500f, 0.50f, 0.72f, 0.65f, 0.35f, 0.70f, 0.0f, 0}},
    {"Glass",     {0.667f, 0.60f, 0.65f, 0.75f, 0.30f, 0.00f, 0.0f, 0}},
    // A slow shimmer: real bells are never quite round, so partials beat.
    {"Bell",      {0.833f, 0.50f, 0.80f, 0.60f, 0.30f, 0.15f, 0.0f, 0}},
    {"Gong",      {1.000f, 0.45f, 0.92f, 0.25f, 0.20f, 0.40f, 0.0f, 0}}, // best low (E2) and high
    {"Felt",      {0.180f, 0.30f, 0.75f, 0.05f, 0.35f, 0.00f, 0.0f, 0}},
    {"Snare bar", {0.000f, 0.55f, 0.28f, 0.65f, 0.45f, 0.00f, 0.3f, 0}}, // rattle
    {"Muffled",   {0.100f, 0.50f, 0.55f, 0.55f, 0.40f, 0.00f, 0.7f, 1}}, // muffle
};
static constexpr int kNumMalletPresets = sizeof(kMalletPresets) / sizeof(kMalletPresets[0]);

} // namespace meter
