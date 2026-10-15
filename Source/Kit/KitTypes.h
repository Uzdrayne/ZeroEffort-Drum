#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

namespace kit
{
// Microphone role used by the kit analysis. Derived from the drum instrument chosen in the
// instance (drum::DrumInstrument index) plus the kit microphone position parameter.
enum class Role { none, kickIn, kickOut, snareTop, snareBottom, tom, floorTom, hiHat, overhead, room };

enum class Mode { off, suggest, automatic };

// Values of the "kitMic" parameter. Their meaning depends on the instrument.
enum Position { positionMain = 0, positionSecondary = 1, positionLeft = 2, positionRight = 3 };

inline constexpr int groupCount = 8;
inline constexpr int maxMembers = 64;
inline constexpr float maxDelayMs = 20.0f;

inline Role roleFor(int instrument, int position) noexcept
{
    switch (instrument)
    {
        case 0: return position == positionSecondary ? Role::kickOut : Role::kickIn;
        case 1: return position == positionSecondary ? Role::snareBottom : Role::snareTop;
        case 2: case 3: case 4: return Role::tom;
        case 5: return Role::floorTom;
        case 6: return Role::hiHat;
        case 7: return Role::overhead;
        case 8: return Role::room;
        default: return Role::none;
    }
}

inline bool isDistant(Role role) noexcept { return role == Role::overhead || role == Role::room; }
inline bool isSecondary(Role role) noexcept { return role == Role::kickOut || role == Role::snareBottom; }
inline bool isClose(Role role) noexcept { return role != Role::none && !isDistant(role); }
inline Role primaryOf(Role role) noexcept
{
    return role == Role::kickOut ? Role::kickIn : role == Role::snareBottom ? Role::snareTop : role;
}

inline const char* roleName(Role role) noexcept
{
    switch (role)
    {
        case Role::kickIn: return "Kick In";
        case Role::kickOut: return "Kick Out";
        case Role::snareTop: return "Snare Top";
        case Role::snareBottom: return "Snare Bottom";
        case Role::tom: return "Tom";
        case Role::floorTom: return "Tom basse";
        case Role::hiHat: return "Hi-hat";
        case Role::overhead: return "Overheads";
        case Role::room: return "Room";
        case Role::none: break;
    }
    return "Hors kit";
}

// Names of the kitMic choices for one instrument; empty when the position is meaningless.
inline juce::StringArray positionNames(int instrument)
{
    switch (instrument)
    {
        case 0: return { "Kick In", "Kick Out" };
        case 1: return { "Snare Top", "Snare Bottom" };
        case 7: case 8: return { "Stereo (2 canaux)", "", "Gauche (L)", "Droite (R)" };
        default: return {};
    }
}

struct Identity
{
    int group = 0;
    int instrument = 0;
    int position = 0;
    Role role = Role::kickIn;
    juce::String name;
    juce::String key;           // track identity shared by the ARA editor and playback instances
    bool operator==(const Identity& other) const noexcept
    {
        return group == other.group && instrument == other.instrument && position == other.position
            && role == other.role && name == other.name && key == other.key;
    }
    bool operator!=(const Identity& other) const noexcept { return !(*this == other); }
};

// One correction proposed for one member of the kit.
struct Recommendation
{
    juce::uint32 memberId = 0;
    juce::String key;
    juce::String label;
    Role role = Role::none;
    bool measured = false;      // enough confident measurements
    bool invert = false;
    float delayMs = 0;          // delay to apply on this track (close mics are delayed onto the overheads)
    float measuredMs = 0;       // raw arrival difference shown to the user
    float confidence = 0;       // |r| of the main measurement
    juce::String note;
};

struct Scores
{
    float phaseBefore = -1, phaseAfter = -1;      // -1 = unavailable
    float stereo = -1;
    float roomBefore = -1, roomAfter = -1;
};

struct Report
{
    bool valid = false;
    bool final = false;
    float progress = 0;
    int events = 0;
    std::vector<Recommendation> recommendations;
    Scores scores;
    juce::StringArray lines;
    juce::String headline;

    const Recommendation* find(juce::uint32 memberId) const noexcept
    {
        for (const auto& recommendation : recommendations)
            if (recommendation.memberId == memberId) return &recommendation;
        return nullptr;
    }

    const Recommendation* findTrack(const juce::String& key) const noexcept
    {
        if (key.isNotEmpty())
            for (const auto& recommendation : recommendations)
                if (recommendation.key == key) return &recommendation;
        return nullptr;
    }
};

struct MemberView
{
    juce::uint32 id = 0;
    Identity identity;
    bool live = false;          // audio written recently with a valid timeline
    bool hasAudio = false;      // has processed timeline audio at least once
    int channels = 0;
    double sampleRate = 0;
};

struct GroupView
{
    int group = 0;
    std::vector<MemberView> members;
    bool analysing = false;
    juce::String status;
    Report report;
    juce::uint32 reportRevision = 0;
};
}
