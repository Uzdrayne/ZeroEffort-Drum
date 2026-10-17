#pragma once

#include "../Drum/DrumEngine.h"

// Display names that follow the drum role chosen in the instance. Parameter IDs and host names
// (drum::controlSpecs) never change; only what the user reads in the editor does.
namespace drum::ui
{
enum class Family { kick, snare, tom, hiHat, overheads, room, master };

inline Family familyOf(int instrument) noexcept
{
    switch (instrument)
    {
        case 0: return Family::kick;
        case 1: return Family::snare;
        case 2: case 3: case 4: case 5: return Family::tom;
        case 6: return Family::hiHat;
        case 7: return Family::overheads;
        case 8: return Family::room;
        default: return Family::master;
    }
}

struct NamedBand { const char* name; const char* hint; };

// The four bell bands keep their fixed ranges (30-200 Hz, 150-1200 Hz, 1-8 kHz, 4-16 kHz);
// their musical meaning depends on the source.
inline std::array<NamedBand, 4> eqBands(int instrument) noexcept
{
    switch (familyOf(instrument))
    {
        case Family::kick: return {{ { "Weight", "fundamental / sub" }, { "Box", "wooden resonance" },
                                     { "Beater", "beater click" }, { "Air", "brightness" } }};
        case Family::snare: return {{ { "Body", "shell fundamental" }, { "Ring", "resonance / box" },
                                      { "Crack", "stick attack" }, { "Snares", "snare wire grain" } }};
        case Family::tom: return {{ { "Fundamental", "shell note" }, { "Box", "wooden resonance" },
                                    { "Attack", "stick" }, { "Air", "brightness" } }};
        case Family::hiHat: return {{ { "Bleed", "Kick / Snare lows" }, { "Gong", "metallic body" },
                                      { "Harshness", "aggressive highs" }, { "Brilliance", "air" } }};
        case Family::overheads: return {{ { "Low", "Kick and Toms" }, { "Mud", "blurry low-mids" },
                                          { "Presence", "Snare and attacks" }, { "Brilliance", "cymbals" } }};
        case Family::room: return {{ { "Low", "room weight" }, { "Mud", "blurry low-mids" },
                                     { "Presence", "attacks" }, { "Air", "clear reverberation" } }};
        case Family::master: break;
    }
    return {{ { "Weight", "Kick and Toms" }, { "Low-mid", "thickness" }, { "Presence", "attacks" }, { "Air", "cymbals" } }};
}

inline std::array<const char*, 2> dynamicBands(int instrument) noexcept
{
    switch (familyOf(instrument))
    {
        case Family::kick: return { "Resonance", "Click" };
        case Family::snare: return { "Ring", "Harshness" };
        case Family::tom: return { "Resonance", "Attack" };
        case Family::hiHat: return { "Gong", "Harshness" };
        default: return { "Resonance", "Cymbals" };
    }
}

inline std::array<const char*, 2> filterHints(int instrument) noexcept
{
    switch (familyOf(instrument))
    {
        case Family::kick: return { "infra below the fundamental", "cymbal bleed" };
        case Family::snare: return { "Kick bleed", "hiss and cymbals" };
        case Family::tom: return { "infra", "cymbals" };
        case Family::hiHat: return { "Kick and Snare", "hiss" };
        case Family::overheads: return { "Kick and lows", "hiss" };
        case Family::room: return { "rumble", "hiss" };
        case Family::master: break;
    }
    return { "infra", "hiss" };
}

// Name of the detection / cleanup page; empty when the page is irrelevant for the role.
inline juce::String focusPageName(int instrument)
{
    const auto family = familyOf(instrument);
    return family == Family::kick ? "Focus / Presence" : family == Family::snare || family == Family::tom ? "Cleanup" : "";
}

inline bool expanderDiscouraged(int instrument) noexcept
{
    const auto family = familyOf(instrument);
    return family == Family::hiHat || family == Family::overheads || family == Family::room || family == Family::master;
}

inline juce::String roleHint(int instrument)
{
    switch (familyOf(instrument))
    {
        case Family::kick: return "Kick: weight, box, beater click";
        case Family::snare: return "Snare: body, ring, crack and snares";
        case Family::tom: return "Toms: shell note, box and stick";
        case Family::hiHat: return "Hi-hat: remove bleed, soften harshness";
        case Family::overheads: return "Overheads: kit image and cymbals";
        case Family::room: return "Room: depth and ambience";
        case Family::master: break;
    }
    return "Drum bus: cohesion of the whole kit";
}

// Short knob caption shown above each control.
inline juce::String knobName(int control, int)
{
    switch (control)
    {
        case input: return "Input";
        case output: return "Output";
        case highPass: return "Low cut";
        case lowPass: return "High cut";
        case gateThreshold: case threshold: case glueThreshold: case crushThreshold: return "Threshold";
        case gateRatio: case ratio: case glueRatio: case crushRatio: return "Ratio";
        case gateRange: return "Range";
        case gateAttack: case attack: case glueAttack: case crushAttack: case shapeAttack: return "Attack";
        case gateRelease: case release: case glueRelease: case crushRelease: return "Release";
        case bodyFrequency: case boxFrequency: case attackFrequency: case airFrequency: return "Frequency";
        case bodyGain: case boxGain: case attackGain: case airGain: return "Gain";
        case bodyQ: case boxQ: case attackQ: case airQ: return "Q";
        case eqQ: return "Global Q";
        case makeup: case glueMakeup: return "Makeup";
        case mix: case glueMix: return "Mix";
        case focusLow: return "Detect HP";
        case focusHigh: return "Detect LP";
        case gateHold: return "Hold";
        case gateHysteresis: return "Hysteresis";
        case cleanupDepth: return "Depth";
        case reinforceFrequency: return "Tune";
        case reinforceAmount: return "Amount";
        case reinforceDecay: return "Decay";
        case transientAmount: return "Punch";
        case saturationDrive: return "Drive";
        case crossoverLow: return "Low / Mid";
        case crossoverHigh: return "Mid / High";
        case lowThreshold: case midThreshold: case highThreshold: return "Threshold";
        case lowRatio: case midRatio: case highRatio: return "Ratio";
        case lowAttack: case midAttack: case highAttack: return "Attack";
        case lowRelease: case midRelease: case highRelease: return "Release";
        case lowMakeup: case midMakeup: case highMakeup: return "Makeup";
        case lowRange: case midRange: case highRange: case glueRange: return "Max GR";
        case glueSidechain: return "Sidechain HP";
        case glueKnee: return "Knee";
        case ambienceAmount: return "Amount";
        case ambienceSize: return "Size";
        case ambiencePredelay: return "Pre-delay";
        case ambienceHighPass: case crushHP: return "Low cut";
        case ambienceLowPass: case crushLP: return "High cut";
        case ambienceCharacter: return "Character";
        case ambienceCompression: return "Density";
        case crushAmount: case colourMix: case clipMix: return "Amount";
        case colourDrive: return "Drive";
        case clipCeiling: return "Ceiling";
        case shapeSustain: return "Sustain";
        case dynamicLowHz: case dynamicHighHz: return "Frequency";
        case dynamicLowQ: case dynamicHighQ: return "Q";
        case dynamicLowThreshold: case dynamicHighThreshold: return "Threshold";
        case dynamicLowRange: case dynamicHighRange: return "Range";
        default: break;
    }
    return controlSpecs[static_cast<size_t>(control)].name;
}

// Full name for tooltips: knob caption plus the role-specific band when relevant.
inline juce::String longName(int control, int instrument)
{
    const auto bands = eqBands(instrument);
    const auto dynamic = dynamicBands(instrument);
    const auto band = [&](int index) { return juce::String(bands[static_cast<size_t>(index)].name); };
    switch (control)
    {
        case bodyFrequency: case bodyGain: case bodyQ: return band(0) + " " + knobName(control, instrument);
        case boxFrequency: case boxGain: case boxQ: return band(1) + " " + knobName(control, instrument);
        case attackFrequency: case attackGain: case attackQ: return band(2) + " " + knobName(control, instrument);
        case airFrequency: case airGain: case airQ: return band(3) + " " + knobName(control, instrument);
        case dynamicLowHz: case dynamicLowQ: case dynamicLowThreshold: case dynamicLowRange:
            return juce::String(dynamic[0]) + " " + knobName(control, instrument);
        case dynamicHighHz: case dynamicHighQ: case dynamicHighThreshold: case dynamicHighRange:
            return juce::String(dynamic[1]) + " " + knobName(control, instrument);
        case highPass: return "Low cut (" + juce::String(filterHints(instrument)[0]) + ")";
        case lowPass: return "High cut (" + juce::String(filterHints(instrument)[1]) + ")";
        default: break;
    }
    return knobName(control, instrument);
}

// Module switch captions.
inline juce::String switchName(int control)
{
    switch (control)
    {
        case bypass: return "BYPASS";
        case filtersOn: return "FILTERS";
        case gateOn: return "EXPANDER";
        case eqOn: return "EQUALIZER";
        case compressorOn: return "COMPRESSOR";
        case multibandOn: return "MULTIBAND";
        case focusOn: return "DETECTION";
        case cleanupOn: return "CLEANUP";
        case presenceOn: return "BODY / PRESENCE";
        case reinforceInvert: return "Body polarity";
        case glueOn: return "GLUE";
        case ambienceOn: return "AMBIENCE";
        case crushOn: return "CRUSH";
        case colourOn: return "COLOR";
        case clipOn: return "SOFT CLIP";
        case shapeOn: return "ATTACK / SUSTAIN";
        case dynamicOn: return "DYNAMIC EQ";
        case autoMeasuredOn: return "Measured body (Auto)";
        case levelMatchOn: return "Level A/B";
        case monitorMonoOn: return "Mono";
        case lowSolo: case midSolo: case highSolo: return "Solo";
        default: break;
    }
    return controlSpecs[static_cast<size_t>(control)].name;
}
}
