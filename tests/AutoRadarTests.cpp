#include "Drum/DrumAutoRadar.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr double rate = 48000;
constexpr double twoPi = juce::MathConstants<double>::twoPi;

void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

// Kicks, snares and a hat with energy in every band, over a quiet floor.
std::vector<float> take(double seconds)
{
    juce::Random random(31);
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto kickAge = std::fmod(time, 0.5), snareAge = std::fmod(time + 0.25, 0.5), hatAge = std::fmod(time, 0.125);
        audio[index] = static_cast<float>(0.5 * std::exp(-kickAge * 22) * std::sin(twoPi * 58 * time)
            + 0.35 * std::exp(-snareAge * 18) * (0.5 * std::sin(twoPi * 200 * time) + random.nextDouble() * 2 - 1)
            + 0.15 * std::exp(-hatAge * 70) * (random.nextDouble() * 2 - 1)
            + 0.001 * (random.nextDouble() * 2 - 1));
    }
    return audio;
}

std::unique_ptr<drum::DrumAnalyser> analyse(const std::vector<float>& audio)
{
    auto analyser = std::make_unique<drum::DrumAnalyser>();
    analyser->prepare(rate);
    for (size_t offset = 0; offset < audio.size(); offset += 4096)
        analyser->push(audio.data() + offset, static_cast<int>(juce::jmin<size_t>(4096, audio.size() - offset)));
    return analyser;
}

void testAxes()
{
    const auto analyser = analyse(take(12));
    const auto& analysis = analyser->analysis();
    const auto curve = drum::DrumAutoRadar::measure(analysis);
    for (size_t axis = 0; axis < drum::radarAxisCount; ++axis)
    {
        check(std::isfinite(curve.raw[axis]), juce::String("Axis ") + drum::radarAxes[axis].id + " must be measurable on a normal take");
        const auto unit = curve.unit(axis);
        check(unit >= 0.0f && unit <= 1.0f, juce::String("Axis ") + drum::radarAxes[axis].id + " must map into 0..1, got " + juce::String(unit));
    }
    check(curve.unit(1) > curve.unit(6) && curve.raw[10] > 3.0f && curve.raw[10] < 5.0f,
        "A kick and snare take is low heavy and plays four hits a second (a kick and a snare every 0.5 s): grave " + juce::String(curve.raw[1]) + " air "
        + juce::String(curve.raw[6]) + " hits/s " + juce::String(curve.raw[10]));

    // An empty analysis has no curve: nothing is invented
    const auto empty = drum::DrumAutoRadar::measure(drum::DrumAnalyser().analysis());
    check(!empty.any() && std::isnan(empty.unit(0)), "An empty analysis must give an empty curve");

    // The scale maps the ends of every axis and clamps outside
    drum::RadarCurve scale;
    scale.raw[7] = 4.0f; scale.raw[8] = 80.0f; scale.raw[9] = 200.0f; scale.raw[0] = -100.0f;
    check(scale.unit(7) == 0.0f && scale.unit(8) == 1.0f && scale.unit(9) == 1.0f && scale.unit(0) == 0.0f && std::isnan(scale.unit(1)),
        "The scale must clamp and keep unavailable values unavailable");
    std::cout << "PASS: radar axes (12 measures in 0..1, empty analysis gives no curve)" << std::endl;
}

void testShapedBands()
{
    const auto analyser = analyse(take(12));
    const auto& analysis = analyser->analysis();
    for (const auto& band : drum::namedBands)
    {
        const auto plain = analysis.bandRelativeDb(band.lower, band.upper);
        const auto unity = analysis.shapedBandRelativeDb(band.lower, band.upper, [](double) { return 1.0; });
        check(std::isfinite(plain) && std::abs(plain - unity) < 1.0e-4f, juce::String("A unity response must equal S02 for ") + band.id);
    }
    // +6 dB above 1 kHz raises the upper bands and lowers the lower ones (they are relative to the total)
    const auto shelf = [](double frequency) { return frequency >= 1000 ? 4.0 : 1.0; };
    const auto lowAfter = analysis.shapedBandRelativeDb(60, 200, shelf), lowBefore = analysis.bandRelativeDb(60, 200);
    const auto highAfter = analysis.shapedBandRelativeDb(5000, 10000, shelf), highBefore = analysis.bandRelativeDb(5000, 10000);
    check(lowAfter < lowBefore - 0.1f && highAfter > highBefore + 0.1f, "A treble boost must lower the low bands and raise the high ones");
    std::cout << "PASS: shaped bands (unity equals S02, a shelf moves the bands the right way)" << std::endl;
}

drum::DrumProposal equaliserProposal()
{
    drum::DrumProposal proposal;
    proposal.valid = true;
    proposal.settings = drum::defaultSettings();
    proposal.settings[drum::eqOn] = 1;
    proposal.settings[drum::attackFrequency] = 3000;
    proposal.settings[drum::attackGain] = 6;
    proposal.settings[drum::attackQ] = 1;
    proposal.settings[drum::bodyGain] = 0;
    proposal.settings[drum::boxGain] = 0;
    proposal.settings[drum::airGain] = 0;
    return proposal;
}

void testIntention()
{
    const auto audio = take(12);
    const auto analyser = analyse(audio);
    const auto& analysis = analyser->analysis();
    const auto source = drum::DrumAutoRadar::measure(analysis);

    const auto proposal = equaliserProposal();
    const auto aimed = drum::DrumAutoRadar::intention(analysis, proposal);
    check(aimed.raw[4] > source.raw[4] + 1.0f, "A +6 dB boost at 3 kHz must raise the presence axis, " + juce::String(source.raw[4]) + " -> " + juce::String(aimed.raw[4]));
    check(aimed.raw[1] < source.raw[1], "The same boost lowers the grave axis (relative to the total)");
    check(aimed.raw[10] == source.raw[10] && aimed.raw[11] == source.raw[11], "Density and decay are not changed by a proposal");

    // The aimed bands are a calculation: the measured ones, on the really processed signal, must agree
    drum::DrumPresetPreview preview;
    preview.prepare(rate, 1, proposal);
    preview.enableOutputAnalysis(rate);
    check(preview.outputAnalysis() != nullptr, "The output analysis must exist once enabled");
    juce::AudioBuffer<float> buffer(1, 4096);
    for (size_t offset = 0; offset < audio.size(); offset += 4096)
    {
        const auto count = static_cast<int>(juce::jmin<size_t>(4096, audio.size() - offset));
        buffer.clear();
        for (int sample = 0; sample < count; ++sample) buffer.setSample(0, sample, audio[offset + static_cast<size_t>(sample)]);
        preview.push(buffer, count);
    }
    const auto obtained = drum::DrumAutoRadar::measure(*preview.outputAnalysis());
    float worst = 0;
    for (size_t band = 0; band < drum::namedBands.size(); ++band)
        worst = juce::jmax(worst, std::abs(obtained.raw[band] - aimed.raw[band]));
    check(worst < 1.5f, "The aimed bands must match the measured ones for an EQ-only proposal, worst " + juce::String(worst) + " dB");
    check(obtained.raw[4] > source.raw[4] + 1.0f, "The processed signal must show the presence boost");

    // Declared effects of the other modules
    auto dynamic = proposal;
    dynamic.settings[drum::compressorOn] = 1;
    dynamic.settings[drum::gateOn] = 1;
    dynamic.settings[drum::gateRange] = 12;
    dynamic.settings[drum::shapeOn] = 1;
    dynamic.settings[drum::shapeAttack] = 3;
    dynamic.settings[drum::shapeSustain] = -1;
    dynamic.targetReduction = 4;
    const auto declared = drum::DrumAutoRadar::intention(analysis, dynamic);
    check(std::abs((declared.raw[7] - aimed.raw[7]) + 4.0f) < 1.0e-4f && std::abs((declared.raw[8] - aimed.raw[8]) - 12.0f) < 1.0e-4f
        && std::abs((declared.raw[9] - aimed.raw[9]) - 4.0f) < 1.0e-4f, "Compression, expander and shaping must move crest, range and attack as declared");
    check(drum::DrumAutoRadar::intention(analysis, drum::DrumProposal {}).raw[4] == source.raw[4], "An invalid proposal aims at nothing: the source is returned");
    std::cout << "PASS: radar intention (EQ calculation agrees with the processed signal within " << worst << " dB)" << std::endl;
}

void testChanges()
{
    auto before = drum::defaultSettings();
    auto after = before;
    after[drum::bypass] = 1;                  // bookkeeping, ignored
    after[drum::instrument] = 3;              // ignored
    after[drum::kitDelay] = 5;                // ignored
    after[drum::eqOn] = 1;                    // a switch counts as 1
    after[drum::bodyGain] = 4;                // 4 of a 48 dB span
    after[drum::highPass] = before[drum::highPass] + 0.1f;   // negligible
    after[drum::attackGain] = 6;
    after[drum::bodyQ] = 2;                   // a Q factor is not a primary setting
    const auto changes = drum::DrumAutoRadar::significantChanges(before, after);
    check(changes.size() == 3 && changes[0].control == drum::eqOn && changes[0].significance == 1.0f,
        "A switch comes first and noise is dropped, got " + juce::String(static_cast<int>(changes.size())) + " changes");
    check(changes[1].control == drum::attackGain && changes[2].control == drum::bodyGain, "Changes must be ordered by their share of the range");
    check(drum::DrumAutoRadar::significantChanges(before, after, 2).size() == 2, "The list must be capped");
    check(drum::DrumAutoRadar::significantChanges(before, before).empty(), "No change gives an empty list");
    std::cout << "PASS: radar changes (ordered, capped, bookkeeping controls ignored)" << std::endl;
}
}

void testAutoRadar()
{
    testAxes();
    testShapedBands();
    testIntention();
    testChanges();
}
