// Non-regression net for the role registry (document 06, section 5): the proposals of every role, style and intensity,
// on three fixed synthetic takes, are frozen as hashes of their quantised settings. The hashes were taken from the code
// BEFORE the registry migration; a migration step must leave them untouched. Set ZED_ROLE_REFERENCE_PRINT=<path of
// tests/RoleReferenceHashes.h> to rewrite the table instead of checking it (only on purpose, after an intended change).
#include "Drum/DrumAnalysisSnapshot.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr double rate = 48000;

void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

std::vector<float> kickTake()
{
    juce::Random random(11);
    std::vector<float> audio(static_cast<size_t>(rate * 8));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, 0.5);
        auto value = 0.6 * std::exp(-age * 28) * std::sin(twoPi * 60 * time) + 0.0004 * (random.nextDouble() * 2 - 1);
        for (const auto frequency : { 2500.0, 3000.0, 3500.0 }) value += 0.05 * std::exp(-age * 400) * std::sin(twoPi * frequency * time);
        audio[index] = static_cast<float>(value);
    }
    return audio;
}

std::vector<float> snareTake()
{
    juce::Random random(12);
    std::vector<float> audio(static_cast<size_t>(rate * 8));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time + 0.25, 0.5);
        const auto noise = random.nextDouble() * 2 - 1;
        audio[index] = static_cast<float>(0.5 * std::exp(-age * 22) * (0.6 * std::sin(twoPi * 200 * time) + 0.8 * noise) + 0.0006 * (random.nextDouble() * 2 - 1));
    }
    return audio;
}

std::vector<float> hatTake()
{
    juce::Random random(13);
    std::vector<float> audio(static_cast<size_t>(rate * 8));
    double previous = 0;
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, 0.125);
        const auto noise = random.nextDouble() * 2 - 1;
        audio[index] = static_cast<float>(0.35 * std::exp(-age * 60) * (noise - 0.9 * previous) + 0.0003 * (random.nextDouble() * 2 - 1));
        previous = noise;
    }
    return audio;
}

std::uint64_t hashProposal(const drum::DrumProposal& proposal)
{
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::int64_t value) {
        for (int shift = 0; shift < 64; shift += 8)
        {
            hash ^= static_cast<std::uint64_t>((value >> shift) & 0xff);
            hash *= 1099511628211ULL;
        }
    };
    mix(proposal.valid ? 1 : 0);
    mix(proposal.hits);
    mix(std::llround(static_cast<double>(proposal.targetReduction) * 1.0e4));
    mix(std::llround(static_cast<double>(proposal.maximumReduction) * 1.0e4));
    for (const auto value : proposal.settings) mix(std::llround(static_cast<double>(value) * 1.0e4));
    for (const auto value : proposal.bandTargets) mix(std::llround(static_cast<double>(value) * 1.0e4));
    for (const auto value : proposal.bandCeilings) mix(std::llround(static_cast<double>(value) * 1.0e4));
    for (const auto character : proposal.summary.toStdString()) mix(static_cast<unsigned char>(character));
    return hash;
}

#include "RoleReferenceHashes.h"
}

void testRoleReference()
{
    const std::array<std::vector<float>, 3> takes { kickTake(), snareTake(), hatTake() };
    const auto printTo = juce::SystemStats::getEnvironmentVariable("ZED_ROLE_REFERENCE_PRINT", {});
    juce::String table;
    size_t index = 0;
    for (size_t take = 0; take < takes.size(); ++take)
    {
        drum::DrumAnalyser analyser;
        analyser.prepare(rate);
        for (size_t offset = 0; offset < takes[take].size(); offset += 4096)
            analyser.push(takes[take].data() + offset, static_cast<int>(juce::jmin(size_t { 4096 }, takes[take].size() - offset)));
        for (int role = 0; role < 10; ++role)
            for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
                for (const auto intensity : { 0.3f, 0.8f, 1.0f })
                {
                    auto original = drum::defaultSettings();
                    original[drum::instrument] = static_cast<float>(role);
                    // The net was frozen with the options of the time (Natural, 100 %); they are copied into the proposal.
                    original[drum::autoStyleOption] = 0;
                    original[drum::autoIntensityOption] = 100;
                    const auto hash = hashProposal(analyser.propose(original, style, intensity));
                    if (printTo.isNotEmpty())
                        table << "    0x" << juce::String::toHexString(static_cast<juce::int64>(hash)) << "ULL, // take " << static_cast<int>(take)
                              << " role " << role << " style " << static_cast<int>(style) << " intensity " << intensity << "\n";
                    else
                    {
                        check(index < frozenHashes.size(), "Role reference table is too short");
                        check(frozenHashes[index] == hash, "Role reference changed: take " + juce::String(static_cast<int>(take)) + ", role " + juce::String(role)
                            + ", style " + juce::String(static_cast<int>(style)) + ", intensity " + juce::String(intensity)
                            + ". A refactor must not change a proposal.");
                    }
                    ++index;
                }
    }
    if (printTo.isNotEmpty())
    {
        check(juce::File(printTo).replaceWithText("// Generated by ZED_ROLE_REFERENCE_PRINT (tests/RoleReferenceTests.cpp). Do not edit by hand.\n"
            "constexpr std::array<std::uint64_t, 270> frozenHashes {\n" + table + "};\n"), "Cannot write " + printTo);
        std::cout << "Role reference table written to " << printTo << std::endl;
        return;
    }
    check(index == frozenHashes.size(), "The role reference table does not match the number of cases");
    std::cout << "PASS: role reference, " << index << " proposals unchanged (3 takes x 10 roles x 3 styles x 3 intensities)" << std::endl;
}
