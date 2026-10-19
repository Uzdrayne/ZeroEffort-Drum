#pragma once
#include "Profiles/KickProfile.h"
#include "Profiles/SnareProfile.h"
#include "Profiles/TomProfile.h"
#include "Profiles/HiHatProfile.h"
#include "Profiles/OverheadsProfile.h"
#include "Profiles/RoomProfile.h"
#include "Profiles/MasterBusProfile.h"
#include <array>

namespace drum
{
enum class RoleFamily { kick, snare, tom, hiHat, overheads, room, bus };

// Everything that distinguishes one role from another in the Auto proposal, as data (document 06, section 2). The
// profiles keep their own tuning (cut-offs, EQ frequencies, times); the finishing stage and the dispatch read this table.
// Adding a role means adding a row, not branching on the role in the code.
struct RoleDescriptor
{
    using Proposer = DrumProposal (*)(const DrumAnalysis&, const Settings&, DrumStyle, float, const ProposalPolicy&);

    DrumInstrument id;
    RoleFamily family;
    Proposer propose;
    bool closeMic, tom, cymbals, room, bus;
    bool measuredBody;                  // Auto may replace the body frequency by the measured spectral peak
    float measuredBodyLowHz;            // lower edge of that search (the upper edge is 200 Hz)
    float crushAmount, crushRatio, crushAttackMs, crushReleaseMs, crushHighPassHz, crushLowPassHz, crushCrestMinimum;
    float colourDrive, colourMix;
    float shapeAttackDb, shapeSustainDb;
    float dynamicLowHz, dynamicHighHz;  // fallbacks when no resonance stands out
    double lowRelevantFraction, highRelevantFraction;
    float dynamicHighRange;             // dB for a non-resonant but relevant high band
    float clipCrestMinimum, clipMix;
    float cleanupMaximum, cleanupBase;
    float focusLowHz, focusHighHz, gateHoldMs;
    bool ambience;
    float ambienceMaximum, ambienceBase, ambienceSize, ambiencePredelayMs;
};

struct RoleProposers
{
    static DrumProposal kick(const DrumAnalysis& a, const Settings& s, DrumStyle style, float intensity, const ProposalPolicy& policy)
    { return KickProfile::propose(a, s, style, intensity, policy); }
    static DrumProposal snare(const DrumAnalysis& a, const Settings& s, DrumStyle style, float intensity, const ProposalPolicy&)
    { return SnareProfile::propose(a, s, style, intensity); }
    static DrumProposal tom(const DrumAnalysis& a, const Settings& s, DrumStyle style, float intensity, const ProposalPolicy&)
    { return TomProfile::propose(a, s, style, intensity); }
    static DrumProposal hiHat(const DrumAnalysis& a, const Settings& s, DrumStyle style, float intensity, const ProposalPolicy& policy)
    { return HiHatProfile::propose(a, s, style, intensity, policy); }
    static DrumProposal overheads(const DrumAnalysis& a, const Settings& s, DrumStyle style, float intensity, const ProposalPolicy& policy)
    { return OverheadsProfile::propose(a, s, style, intensity, policy); }
    static DrumProposal room(const DrumAnalysis& a, const Settings& s, DrumStyle style, float intensity, const ProposalPolicy&)
    { return RoomProfile::propose(a, s, style, intensity); }
    static DrumProposal bus(const DrumAnalysis& a, const Settings& s, DrumStyle style, float intensity, const ProposalPolicy&)
    { return MasterBusProfile::propose(a, s, style, intensity); }
};

// One row per DrumInstrument, in the order of the enum.
inline constexpr std::array<RoleDescriptor, 10> roleDescriptors {{
    //  id                       family                  propose                  close  tom    cymb   room   bus    measuredBody lowHz
    { DrumInstrument::kick,      RoleFamily::kick,       &RoleProposers::kick,      true,  false, false, false, false, false, 0.0f,
      24.0f, 8.0f, 8.0f, 110.0f, 35.0f, 8000.0f, 5.0f,   9.0f, 28.0f,   2.4f, -1.0f,   300.0f, 6500.0f,  0.035, 0.02, 1.8f,   6.0f, 32.0f,
      18.0f, 10.0f,  70.0f, 400.0f, 35.0f,   false, 100.0f, 7.0f, 22.0f, 16.0f },
    { DrumInstrument::snare,     RoleFamily::snare,      &RoleProposers::snare,     true,  false, false, false, false, true,  100.0f,
      24.0f, 8.0f, 4.0f, 110.0f, 100.0f, 8000.0f, 5.0f,   9.0f, 28.0f,   3.0f, 0.5f,    650.0f, 6500.0f,  0.035, 0.02, 1.8f,   6.0f, 32.0f,
      18.0f, 10.0f,  70.0f, 400.0f, 35.0f,   true,  100.0f, 7.0f, 22.0f, 16.0f },
    { DrumInstrument::tom1,      RoleFamily::tom,        &RoleProposers::tom,       true,  true,  false, false, false, true,  45.0f,
      22.0f, 8.0f, 4.0f, 220.0f, 100.0f, 8000.0f, 5.0f,   9.0f, 28.0f,   3.0f, 1.5f,    450.0f, 6500.0f,  0.035, 0.02, 1.8f,   6.0f, 32.0f,
      12.0f, 7.0f,   40.0f, 260.0f, 60.0f,   true,  100.0f, 5.0f, 35.0f, 16.0f },
    { DrumInstrument::tom2,      RoleFamily::tom,        &RoleProposers::tom,       true,  true,  false, false, false, true,  45.0f,
      22.0f, 8.0f, 4.0f, 220.0f, 100.0f, 8000.0f, 5.0f,   9.0f, 28.0f,   3.0f, 1.5f,    450.0f, 6500.0f,  0.035, 0.02, 1.8f,   6.0f, 32.0f,
      12.0f, 7.0f,   40.0f, 260.0f, 60.0f,   true,  100.0f, 5.0f, 35.0f, 16.0f },
    { DrumInstrument::tom3,      RoleFamily::tom,        &RoleProposers::tom,       true,  true,  false, false, false, true,  45.0f,
      22.0f, 8.0f, 4.0f, 220.0f, 100.0f, 8000.0f, 5.0f,   9.0f, 28.0f,   3.0f, 1.5f,    450.0f, 6500.0f,  0.035, 0.02, 1.8f,   6.0f, 32.0f,
      12.0f, 7.0f,   40.0f, 260.0f, 60.0f,   true,  100.0f, 5.0f, 35.0f, 16.0f },
    { DrumInstrument::floorTom,  RoleFamily::tom,        &RoleProposers::tom,       true,  true,  false, false, false, true,  45.0f,
      22.0f, 8.0f, 4.0f, 220.0f, 100.0f, 8000.0f, 5.0f,   9.0f, 28.0f,   3.0f, 1.5f,    450.0f, 6500.0f,  0.035, 0.02, 1.8f,   6.0f, 32.0f,
      12.0f, 7.0f,   40.0f, 260.0f, 60.0f,   true,  100.0f, 5.0f, 35.0f, 16.0f },
    { DrumInstrument::hiHat,     RoleFamily::hiHat,      &RoleProposers::hiHat,     false, false, true,  false, false, false, 0.0f,
      24.0f, 8.0f, 4.0f, 110.0f, 100.0f, 8000.0f, 5.0f,   9.0f, 28.0f,   3.0f, -1.0f,   650.0f, 8000.0f,  0.15,  0.06, 2.8f,   6.0f, 32.0f,
      18.0f, 10.0f,  70.0f, 400.0f, 35.0f,   false, 100.0f, 7.0f, 22.0f, 16.0f },
    { DrumInstrument::overheads, RoleFamily::overheads,  &RoleProposers::overheads, false, false, true,  false, false, false, 0.0f,
      24.0f, 8.0f, 4.0f, 110.0f, 100.0f, 8000.0f, 5.0f,   9.0f, 28.0f,   3.0f, -1.0f,   650.0f, 8000.0f,  0.15,  0.06, 2.8f,   6.0f, 32.0f,
      18.0f, 10.0f,  70.0f, 400.0f, 35.0f,   false, 100.0f, 7.0f, 22.0f, 16.0f },
    { DrumInstrument::room,      RoleFamily::room,       &RoleProposers::room,      false, false, false, true,  false, false, 0.0f,
      26.0f, 10.0f, 4.0f, 180.0f, 100.0f, 6000.0f, 5.0f,  9.0f, 26.0f,   3.0f, -1.0f,   650.0f, 6500.0f,  0.035, 0.06, 1.8f,   6.0f, 32.0f,
      18.0f, 10.0f,  70.0f, 400.0f, 35.0f,   false, 100.0f, 7.0f, 22.0f, 16.0f },
    { DrumInstrument::master,    RoleFamily::bus,        &RoleProposers::bus,       false, false, false, false, true,  false, 0.0f,
      10.0f, 4.0f, 12.0f, 110.0f, 60.0f, 8000.0f, 7.0f,   5.0f, 14.0f,   3.0f, -1.0f,   650.0f, 6500.0f,  0.035, 0.06, 1.8f,   9.0f, 18.0f,
      18.0f, 10.0f,  70.0f, 400.0f, 35.0f,   true,  4.0f,   3.0f, 22.0f, 8.0f },
}};

inline const RoleDescriptor& roleDescriptor(DrumInstrument role) noexcept
{
    return roleDescriptors[static_cast<size_t>(role)];
}
}
