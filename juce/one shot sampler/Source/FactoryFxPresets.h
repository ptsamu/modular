#pragma once

#include <array>

namespace FactoryFxPresets
{
struct Preset
{
    const char* name;
    const char* description;
    std::array<float, 6> values;
};

// Session state stores the zero-based index. Keep published entries in order.
inline constexpr Preset reverb[] {
    { "Space", "Dark compact room", { .20f, .64f, .72f, 0, .16f } },
    { "Chamber", "Open chamber", { .56f, .37f, .94f, 0, .29f } },
    { "Halo", "Bright frozen tail", { .82f, .18f, 1, 1, .38f } },
    { "Drift", "Wide hall", { .92f, .30f, 1, 0, .42f } },
    { "Booth", "Short and narrow", { .08f, .80f, .27f, 0, .19f } },
    { "Tile Room", "Bright short reflections", { .24f, .12f, .66f, 0, .28f } },
    { "Wood Room", "Soft reflections behind the dry hit", { .34f, .68f, .54f, 0, .31f } },
    { "Plate Glow", "Bright, wide plate-style wash", { .62f, .16f, 1, 0, .30f } },
    { "Vocal Hall", "Wide tail with a clear onset", { .75f, .43f, .88f, 0, .24f } },
    { "Velvet Hall", "Long, dark tail", { .84f, .76f, .80f, 0, .36f } },
    { "Cathedral", "Large and bright", { .96f, .25f, 1, 0, .49f } },
    { "Dark Hold", "Frozen texture with a dark tone", { .72f, .78f, .83f, 1, .28f } },
};

inline constexpr Preset chorus[] {
    { "Detune", "Subtle dual voice", { .33f, .28f, 6.5f, .02f, .20f, 0 } },
    { "Wide", "Wide stereo motion", { .72f, .46f, 11, .08f, .28f, 0 } },
    { "VHS", "Slow cassette drift", { .16f, .66f, 17, .14f, .36f, 0 } },
    { "Drift", "Deep ensemble motion", { 1.35f, .74f, 21, .22f, .42f, 0 } },
    { "Micro Double", "Tight doubling with minimal pitch movement", { .23f, .09f, 9, 0, .25f, 0 } },
    { "Silk Ensemble", "Slow, deep movement across the stereo field", { .24f, .82f, 23, .08f, .44f, 0 } },
    { "Liquid Keys", "Medium-speed keyboard chorus", { 1.92f, .53f, 12, .05f, .41f, 0 } },
    { "Rotary Blur", "Fast, shallow modulation", { 4.6f, .28f, 14, .09f, .43f, 0 } },
    { "Tape Drive", "Slow chorus with warm wet-path saturation", { .36f, .54f, 17, .09f, .38f, .31f } },
    { "Broken Deck", "Deep wobble with driven repeats", { .81f, .78f, 22, .23f, .45f, .57f } },
    { "Fuzz Ensemble", "Heavy drive and wide, slow modulation", { .29f, .90f, 26, .12f, .54f, .85f } },
    { "Hot Rotary", "Faster movement with a saturated wet voice", { 3.35f, .42f, 13, .11f, .47f, .68f } },
};

inline constexpr Preset granular[] {
    { "Grain", "Loose, unpitched grains", { 185, 5, 0, .24f } },
    { "Shards", "Short grains shifted up a fourth", { 95, 11, 5, .32f } },
    { "Cloud", "Long octave-down grains", { 340, 2.5f, -12, .40f } },
    { "Dust", "Dense octave-up grains", { 62, 17, 12, .30f } },
    { "Micro Mist", "Very short, dense unpitched grains", { 28, 29, 0, .27f } },
    { "Feather", "Soft upper-octave texture", { 210, 8, 12, .31f } },
    { "Sub Cloud", "Long lower-octave grains", { 420, 2.2f, -12, .38f } },
    { "Glass Fifth", "Bright parallel fifths", { 115, 13, 7, .36f } },
    { "Minor Fragments", "Dense minor-third fragments", { 54, 22, 3, .35f } },
    { "Low Drift", "Sparse downward fifths", { 390, 3.5f, -7, .44f } },
    { "Grain Stack", "Continuous unpitched grain stream", { 85, 12, 0, .48f } },
    { "Spark Trail", "Short grains two octaves up", { 39, 16, 24, .23f } },
};

inline constexpr Preset random[] {
    { "Chance", "Subtle random motion", { .12f, 2, .12f } },
    { "Flux", "Moderate motion at 5 Hz", { .30f, 5, .20f } },
    { "Tilt", "Deeper random motion", { .55f, 9, .28f } },
    { "Ripple", "Fast, pronounced random motion", { .82f, 15, .34f } },
    { "Slow Wander", "Slow and deep", { .52f, 1, .27f } },
    { "Under Current", "Deep movement blended under the source", { .68f, 3, .17f } },
    { "Loose Pulse", "Medium-rate movement with a stronger blend", { .58f, 6, .40f } },
    { "Nervous Air", "Fast, shallow modulation", { .25f, 16, .23f } },
    { "Broken Pulse", "Deep, fast movement", { .91f, 12.5f, .45f } },
    { "Scatter", "Very fast and deep", { .96f, 22, .37f } },
    { "Soft Static", "Fast motion at a low mix", { .44f, 24, .16f } },
    { "Deep Drift", "Strong movement at a slow rate", { .87f, 2.7f, .49f } },
};

inline constexpr Preset eq[] {
    { "Spectrum", "7-band neutral curve", {} },
    { "Low cut", "High-pass at 95 Hz", {} },
    { "Warmth", "Broad warmth", {} },
    { "Air", "High-shelf lift", {} },
};

struct Bank
{
    const Preset* presets;
    int size;
    const char* prefix;
    std::array<const char*, 6> parameters;
};

template <std::size_t Size>
constexpr Bank makeBank (const Preset (&presets)[Size], const char* prefix,
                         std::array<const char*, 6> parameters = {})
{
    return { presets, static_cast<int> (Size), prefix, parameters };
}

inline constexpr Bank bankFor (int effect)
{
    switch (effect)
    {
        case 2: return makeBank (chorus, "chor", { "chorusRate", "chorusDepth", "chorusDelay", "chorusFeedback", "chorusMix", "chorusDrive" });
        case 5: return makeBank (eq, "eq");
        case 7: return makeBank (granular, "gran", { "granularSize", "granularDensity", "granularPitch", "granularMix" });
        case 9: return makeBank (random, "rand", { "randomAmount", "randomRate", "randomMix" });
        default: return makeBank (reverb, "verb", { "reverbRoomSize", "reverbDamping", "reverbWidth", "reverbFreeze", "reverbMix" });
    }
}
}
