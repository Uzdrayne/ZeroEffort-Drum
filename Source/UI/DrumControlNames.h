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
        case Family::kick: return {{ { "Poids", "fondamentale / sub" }, { "Carton", "resonance boisee" },
                                     { "Batte", "clic du beater" }, { "Air", "brillance" } }};
        case Family::snare: return {{ { "Corps", "fondamentale du fut" }, { "Bague", "resonance / boite" },
                                      { "Claque", "attaque de baguette" }, { "Timbre", "grain du timbre" } }};
        case Family::tom: return {{ { "Fondamentale", "note du fut" }, { "Carton", "resonance boisee" },
                                    { "Attaque", "baguette" }, { "Air", "brillance" } }};
        case Family::hiHat: return {{ { "Repisse", "grave Kick / Snare" }, { "Gong", "corps metallique" },
                                      { "Durete", "aigu agressif" }, { "Brillance", "air" } }};
        case Family::overheads: return {{ { "Grave", "Kick et Toms" }, { "Boue", "bas-medium flou" },
                                          { "Presence", "Snare et attaques" }, { "Brillance", "cymbales" } }};
        case Family::room: return {{ { "Grave", "poids de la piece" }, { "Boue", "bas-medium flou" },
                                     { "Presence", "attaques" }, { "Air", "reverberation claire" } }};
        case Family::master: break;
    }
    return {{ { "Poids", "Kick et Toms" }, { "Bas-medium", "epaisseur" }, { "Presence", "attaques" }, { "Air", "cymbales" } }};
}

inline std::array<const char*, 2> dynamicBands(int instrument) noexcept
{
    switch (familyOf(instrument))
    {
        case Family::kick: return { "Resonance", "Clic" };
        case Family::snare: return { "Bague", "Durete" };
        case Family::tom: return { "Resonance", "Attaque" };
        case Family::hiHat: return { "Gong", "Durete" };
        default: return { "Resonance", "Cymbales" };
    }
}

inline std::array<const char*, 2> filterHints(int instrument) noexcept
{
    switch (familyOf(instrument))
    {
        case Family::kick: return { "infra sous la fondamentale", "repisse des cymbales" };
        case Family::snare: return { "repisse du Kick", "souffle et cymbales" };
        case Family::tom: return { "infra", "cymbales" };
        case Family::hiHat: return { "Kick et Snare", "souffle" };
        case Family::overheads: return { "Kick et grave", "souffle" };
        case Family::room: return { "grondement", "souffle" };
        case Family::master: break;
    }
    return { "infra", "souffle" };
}

// Name of the detection / cleanup page; empty when the page is irrelevant for the role.
inline juce::String focusPageName(int instrument)
{
    const auto family = familyOf(instrument);
    return family == Family::kick ? "Focus / Presence" : family == Family::snare || family == Family::tom ? "Nettoyage" : "";
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
        case Family::kick: return "Kick : poids, carton, clic du beater";
        case Family::snare: return "Snare : corps, bague, claque et timbre";
        case Family::tom: return "Toms : note du fut, carton et baguette";
        case Family::hiHat: return "Hi-hat : retirer la repisse, adoucir la durete";
        case Family::overheads: return "Overheads : image du kit et cymbales";
        case Family::room: return "Room : profondeur et ambiance";
        case Family::master: break;
    }
    return "Bus batterie : cohesion de l'ensemble";
}

// Short knob caption shown above each control.
inline juce::String knobName(int control, int)
{
    switch (control)
    {
        case input: return "Entree";
        case output: return "Sortie";
        case highPass: return "Coupe-bas";
        case lowPass: return "Coupe-haut";
        case gateThreshold: case threshold: case glueThreshold: case crushThreshold: return "Seuil";
        case gateRatio: case ratio: case glueRatio: case crushRatio: return "Ratio";
        case gateRange: return "Plage";
        case gateAttack: case attack: case glueAttack: case crushAttack: case shapeAttack: return "Attaque";
        case gateRelease: case release: case glueRelease: case crushRelease: return "Relache";
        case bodyFrequency: case boxFrequency: case attackFrequency: case airFrequency: return "Frequence";
        case bodyGain: case boxGain: case attackGain: case airGain: return "Gain";
        case bodyQ: case boxQ: case attackQ: case airQ: return "Q";
        case eqQ: return "Q global";
        case makeup: case glueMakeup: return "Makeup";
        case mix: case glueMix: return "Mix";
        case focusLow: return "Detection HP";
        case focusHigh: return "Detection LP";
        case gateHold: return "Maintien";
        case gateHysteresis: return "Hysteresis";
        case cleanupDepth: return "Profondeur";
        case reinforceFrequency: return "Accord";
        case reinforceAmount: return "Dose";
        case reinforceDecay: return "Decroissance";
        case transientAmount: return "Punch";
        case saturationDrive: return "Drive";
        case crossoverLow: return "Grave / Medium";
        case crossoverHigh: return "Medium / Aigu";
        case lowThreshold: case midThreshold: case highThreshold: return "Seuil";
        case lowRatio: case midRatio: case highRatio: return "Ratio";
        case lowAttack: case midAttack: case highAttack: return "Attaque";
        case lowRelease: case midRelease: case highRelease: return "Relache";
        case lowMakeup: case midMakeup: case highMakeup: return "Makeup";
        case lowRange: case midRange: case highRange: case glueRange: return "GR max";
        case glueSidechain: return "Sidechain HP";
        case glueKnee: return "Knee";
        case ambienceAmount: return "Dose";
        case ambienceSize: return "Taille";
        case ambiencePredelay: return "Pre-delai";
        case ambienceHighPass: case crushHP: return "Coupe-bas";
        case ambienceLowPass: case crushLP: return "Coupe-haut";
        case ambienceCharacter: return "Caractere";
        case ambienceCompression: return "Densite";
        case crushAmount: case colourMix: case clipMix: return "Dose";
        case colourDrive: return "Drive";
        case clipCeiling: return "Reference";
        case shapeSustain: return "Sustain";
        case dynamicLowHz: case dynamicHighHz: return "Frequence";
        case dynamicLowQ: case dynamicHighQ: return "Q";
        case dynamicLowThreshold: case dynamicHighThreshold: return "Seuil";
        case dynamicLowRange: case dynamicHighRange: return "Plage";
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
        case highPass: return "Coupe-bas (" + juce::String(filterHints(instrument)[0]) + ")";
        case lowPass: return "Coupe-haut (" + juce::String(filterHints(instrument)[1]) + ")";
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
        case filtersOn: return "FILTRES";
        case gateOn: return "EXPANDER";
        case eqOn: return "EGALISEUR";
        case compressorOn: return "COMPRESSEUR";
        case multibandOn: return "MULTIBANDE";
        case focusOn: return "DETECTION";
        case cleanupOn: return "NETTOYAGE";
        case presenceOn: return "CORPS / PRESENCE";
        case reinforceInvert: return "Polarite du corps";
        case glueOn: return "GLUE";
        case ambienceOn: return "AMBIANCE";
        case crushOn: return "CRUSH";
        case colourOn: return "COULEUR";
        case clipOn: return "SOFT CLIP";
        case shapeOn: return "ATTACK / SUSTAIN";
        case dynamicOn: return "EQ DYNAMIQUE";
        case autoMeasuredOn: return "Corps mesure (Auto)";
        case levelMatchOn: return "Niveau A/B";
        case monitorMonoOn: return "Mono";
        case lowSolo: case midSolo: case highSolo: return "Solo";
        default: break;
    }
    return controlSpecs[static_cast<size_t>(control)].name;
}
}
