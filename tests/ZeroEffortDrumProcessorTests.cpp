#include "ARA/ZeroEffortDrumProcessor.h"
#include "Drum/DrumEngine.h"
#include "Drum/DrumArchive.h"
#include "Drum/DrumAutoPreset.h"
#include "Drum/DrumPresetPreview.h"
#include "Drum/DrumFilterResponse.h"
#include "Drum/KickPresenceDSP.h"
#include "LegacyDrumProposalReference.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory();
void testDrumBus();
void testKit();
void testSnapshot();
void testAnalysisProperties();
void testReplay();
void testMeasures();
void testKnownProblems();
void testRoleReference();
void testSignals();
int runReplayCommand(const juce::StringArray& arguments);
int runKitCommand(const juce::StringArray& arguments);
int runPairCommand(const juce::StringArray& arguments);
int runBusCommand(const juce::StringArray& arguments);
int runVersusCommand(const juce::StringArray& arguments);
int runSubSumCommand(const juce::StringArray& arguments);
void testVersus();
void testPair();
void testAutoRadar();
void testPresets();
void testSubBuses();
void makeScreenshots(const juce::File& directory);
void testEditor(ZeroEffortDrumProcessor& processor, const juce::File& imageFile);
void testAutoRadarFlow(const juce::File& imageFile);

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void fill(juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample(channel, sample, 0.25f);
}

float measureTone(const drum::Settings& settings, double frequency, double sampleRate = 48000)
{
    drum::DrumEngine engine;
    engine.prepare(sampleRate, 256, 2);
    juce::AudioBuffer<float> audio(2, 256);
    double energy = 0;
    const auto measurementSamples = static_cast<int>(sampleRate);
    for (int block = 0; block < (60 * 256 + measurementSamples + 255) / 256; ++block)
    {
        for (int sample = 0; sample < 256; ++sample)
        {
            const auto value = 0.1f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi
                * frequency * (block * 256 + sample) / sampleRate));
            audio.setSample(0, sample, value);
            audio.setSample(1, sample, value * 0.5f);
        }
        engine.process(audio, settings);
        for (int sample = 0; sample < 256; ++sample)
        {
            require(std::isfinite(audio.getSample(0, sample)), "DSP output must remain finite");
            require(std::abs(audio.getSample(1, sample) - 0.5f * audio.getSample(0, sample)) < 0.00001f,
                "Shared dynamics must preserve stereo level relationships");
            if (block >= 60 && block * 256 + sample < 60 * 256 + measurementSamples)
                energy += audio.getSample(0, sample) * audio.getSample(0, sample);
        }
    }
    return static_cast<float>(std::sqrt(energy / measurementSamples));
}

// The global Makeup belongs to whichever compressor is running: the broadband one, or the multiband one (as a master makeup on top of the
// makeup of each band). Without the Compressor switch it does nothing.
void testMakeupScope()
{
    const auto base = measureTone(drum::defaultSettings(), 1000);
    const auto gainDb = [&](const drum::Settings& settings) { return 20.0f * std::log10(measureTone(settings, 1000) / base); };
    const auto quiet = [] {
        auto settings = drum::defaultSettings();
        settings[drum::threshold] = 0;
        for (auto control : { drum::lowThreshold, drum::midThreshold, drum::highThreshold }) settings[control] = 0;
        settings[drum::makeup] = 6;
        return settings;
    }();
    auto settings = quiet;
    require(std::abs(gainDb(settings)) < 0.05f, "Makeup does nothing while the Compressor switch is off");
    settings[drum::multibandOn] = 1;
    require(std::abs(gainDb(settings)) < 0.05f, "Makeup does nothing in multiband mode while the Compressor switch is off");
    settings = quiet;
    settings[drum::compressorOn] = 1;
    require(std::abs(gainDb(settings) - 6.0f) < 0.2f, "Makeup acts on the broadband compressor");
    settings[drum::multibandOn] = 1;
    require(std::abs(gainDb(settings) - 6.0f) < 0.3f, "In multiband mode the same Makeup acts as a master makeup");
    for (auto control : { drum::lowMakeup, drum::midMakeup, drum::highMakeup }) settings[control] = 3;
    require(std::abs(gainDb(settings) - 9.0f) < 0.3f, "The master makeup adds to the makeup of each band");
}

void testKickResponse()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto settings = drum::defaultSettings();
        settings[drum::dynamicOn] = 1;
        settings[drum::dynamicLowRange] = 4;
        settings[drum::dynamicHighRange] = 3;
        const drum::DrumDynamicResponse idle(settings, rate, { 0, 0 });
        const drum::DrumDynamicResponse active(settings, rate, { 4, 3 });
        const drum::DrumDynamicResponse bounded(settings, rate, { 40, 30 });
        require(std::abs(idle.magnitude(350) - 1) < 0.000001
            && active.magnitude(350) < 0.65 && active.magnitude(7000) < 0.72,
            "Dynamic graph must follow actual reductions at both band frequencies rather than a fixed curve");
        require(std::abs(active.magnitude(350) - bounded.magnitude(350)) < 0.000001,
            "Dynamic graph must use the same bounded range as the processor");
        settings[drum::bypass] = 1;
        require(std::abs(drum::DrumDynamicResponse(settings, rate, { 4, 3 }).magnitude(350) - 1) < 0.000001,
            "Bypassed dynamic graph must not show applied attenuation");
        settings[drum::bypass] = settings[drum::dynamicOn] = 0;
        settings[drum::compressorOn] = settings[drum::multibandOn] = 1;
        settings[drum::lowRatio] = settings[drum::midRatio] = settings[drum::highRatio] = 1;
        for (const auto frequency : { 50.0, 140.0, 500.0, 2000.0, 8000.0 })
            require(std::abs(measureTone(settings, frequency, rate) / measureTone(drum::defaultSettings(), frequency, rate) - 1) < 0.005f,
                "Phase-compensated three-band crossover must reconstruct flat magnitude at ratio one");
        settings[drum::lowSolo] = 1;
        require(measureTone(settings, 60, rate) / measureTone(drum::defaultSettings(), 60, rate) > 0.96f
            && measureTone(settings, 8000, rate) / measureTone(drum::defaultSettings(), 8000, rate) < 0.001f,
            "Low Solo must retain the kick body and reject the independent high band");
        settings[drum::lowSolo] = 0;
        settings[drum::highSolo] = 1;
        require(measureTone(settings, 60, rate) / measureTone(drum::defaultSettings(), 60, rate) < 0.001f
            && measureTone(settings, 8000, rate) / measureTone(drum::defaultSettings(), 8000, rate) > 0.99f,
            "High Solo must expose the high band without the kick fundamental");
        settings[drum::highSolo] = 0;
        drum::DrumMultiband crossover;
        crossover.prepare(rate, 256, 2);
        drum::MultibandParameters parameters;
        parameters.lowHz = 350;
        parameters.highHz = 6000;
        crossover.configure(parameters);
        std::array<float, 1024> firstRender {};
        for (int pass = 0; pass < 2; ++pass)
        {
            crossover.reset();
            for (size_t sample = 0; sample < firstRender.size(); ++sample)
            {
                const auto value = static_cast<float>(0.3 * std::sin(juce::MathConstants<double>::twoPi * 65 * sample / rate));
                std::array<float, 2> stereo { value, -value };
                crossover.process(stereo, 2, 1, 0);
                require(std::isfinite(stereo[0]) && std::abs(stereo[0] + stereo[1]) < 0.000001f,
                    "Extreme crossover settings must remain finite and preserve opposite stereo channels");
                if (pass == 0) firstRender[sample] = stereo[0];
                else require(stereo[0] == firstRender[sample], "Crossover reset must reproduce the exact render without stale tails");
            }
        }
        settings[drum::lowThreshold] = -50;
        settings[drum::lowRatio] = 4;
        settings[drum::lowRange] = 12;
        require(measureTone(settings, 60, rate) / measureTone(drum::defaultSettings(), 60, rate) < 0.35f,
            "Multiband low compressor must reduce low-frequency energy");
        require(measureTone(settings, 8000, rate) / measureTone(drum::defaultSettings(), 8000, rate) > 0.99f,
            "Low-band compression must not compress the independent high band");
        settings[drum::compressorOn] = 0;
        require(std::abs(measureTone(settings, 60, rate) / measureTone(drum::defaultSettings(), 60, rate) - 1) < 0.0001f,
            "Disabled compressor must bypass the crossover and preserve the original signal");
    }
    {
        drum::SpectrumMailbox mailbox;
        drum::SpectrumCapture capture;
        drum::SpectrumDisplay display;
        juce::AudioBuffer<float> signal(2, 256);
        for (int block = 0; block < 16; ++block)
        {
            for (int sample = 0; sample < 256; ++sample)
            {
                const auto value = 0.25f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi
                    * 750 * (block * 256 + sample) / 48000));
                signal.setSample(0, sample, value);
                signal.setSample(1, sample, -value);
            }
            capture.push(signal, mailbox, 48000);
        }
        require(!display.update(mailbox), "Spectrum must not capture when no editor subscribes");
        mailbox.subscribers.store(1);
        for (int block = 0; block < 16; ++block)
        {
            for (int sample = 0; sample < 256; ++sample)
            {
                const auto value = 0.25f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi
                    * 750 * (block * 256 + sample) / 48000));
                signal.setSample(0, sample, value);
                signal.setSample(1, sample, -value);
            }
            capture.push(signal, mailbox, 48000);
        }
        require(display.update(mailbox) && std::abs(display.magnitudeDb(750) + 12.0412f) < 0.1f,
            "Hann-normalised spectrum must report the sine amplitude without cancelling opposite stereo channels");
        require(display.magnitudeDb(1500) < -80 && !display.update(mailbox),
            "Spectrum mailbox must deliver each frame once and preserve frequency selectivity");
        const auto held = display.magnitudeDb(750);
        for (int tick = 0; tick < 20; ++tick) display.update(mailbox);
        require(display.magnitudeDb(750) < held - 35, "Spectrum must decay when playback stops");
    }
    {
        drum::KickPresenceDSP presence;
        presence.prepare(48000, 256, 2);
        std::array<float, 2> signal { 0.1f, 0.05f };
        presence.detect(signal, 2);
        presence.process(signal, 2, 0, 0);
        require(signal[0] == 0.1f && signal[1] == 0.05f,
            "New presence processing must be bit-transparent when disabled");
        for (int sample = 0; sample < 48000; ++sample)
        {
            signal = { 0, 0 };
            presence.detect(signal, 2);
            presence.process(signal, 2, 1, 1);
            require(signal[0] == 0 && signal[1] == 0, "Silent input must never invent kick hits or a body layer");
        }
        require(presence.hitCount() == 0, "Silence must not trigger the new detector");
    }
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        auto filtered = drum::defaultSettings();
        filtered[drum::filtersOn] = 1;
        filtered[drum::highPass] = 200;
        filtered[drum::lowPass] = 3000;
        const drum::DrumFilterResponse response(filtered, sampleRate);
        for (double frequency : { 50.0, 200.0, 1000.0, 3000.0, 10000.0 })
        {
            const auto measured = measureTone(filtered, frequency, sampleRate)
                / measureTone(drum::defaultSettings(), frequency, sampleRate);
            require(std::abs(measured - response.magnitude(frequency)) < 0.003,
                "Plotted HP/LP response must match real TPT audio filters at supported rates");
        }
        filtered[drum::filtersOn] = 0;
        require(drum::DrumFilterResponse(filtered, sampleRate).magnitude(50) == 1,
            "Disabled filters must leave the plotted response flat");
    }
    auto settings = drum::defaultSettings();
    const auto reference = measureTone(settings, 100);
    settings[drum::output] = 6;
    require(std::abs(measureTone(settings, 100) / reference - juce::Decibels::decibelsToGain(6.0f)) < 0.001f,
        "Manual output must allow gain above zero without clipping internally");
    settings = drum::defaultSettings();
    settings[drum::filtersOn] = 1;
    settings[drum::highPass] = 200;
    require(measureTone(settings, 30) < reference * 0.06f, "High-pass must reject sub-bass below cutoff");
    settings[drum::highPass] = 20;
    settings[drum::lowPass] = 1000;
    require(measureTone(settings, 10000) < reference * 0.03f, "Low-pass must reject high-frequency bleed");
    settings = drum::defaultSettings();
    settings[drum::eqOn] = 1;
    settings[drum::bodyFrequency] = 100;
    settings[drum::bodyGain] = 6;
    require(std::abs(measureTone(settings, 100) / reference - juce::Decibels::decibelsToGain(6.0f)) < 0.02f,
        "Bell EQ must reach the requested centre gain");
    const auto broad = measureTone(settings, 200);
    settings[drum::bodyQ] = 2;
    require(measureTone(settings, 200) < broad * 0.9f,
        "Independent Body Q must narrow the bell without changing other bands");
    require(drum::bandQ(settings, 1) == settings[drum::eqQ], "Old band settings must inherit the legacy global Q");
    settings[drum::bodyQ] = 0;
    settings[drum::bodyGain] = -6;
    require(std::abs(measureTone(settings, 100) / reference - juce::Decibels::decibelsToGain(-6.0f)) < 0.02f,
        "Bell EQ must reach the requested centre cut");
    settings = drum::defaultSettings();
    settings[drum::gateOn] = 1;
    settings[drum::gateThreshold] = 0;
    settings[drum::gateRange] = 18;
    require(std::abs(measureTone(settings, 100) / reference - juce::Decibels::decibelsToGain(-18.0f)) < 0.01f,
        "Expander attenuation must be bounded by range");
    settings[drum::gateRatio] = 1;
    require(std::abs(measureTone(settings, 100) / reference - 1.0f) < 0.01f,
        "Expander ratio one must preserve unity");
    settings = drum::defaultSettings();
    settings[drum::compressorOn] = 1;
    settings[drum::threshold] = -40;
    settings[drum::mix] = 0;
    require(std::abs(measureTone(settings, 100) / reference - 1.0f) < 0.01f,
        "Zero compressor mix must preserve dry signal");
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        settings = drum::defaultSettings();
        settings[drum::filtersOn] = 1;
        settings[drum::eqOn] = 1;
        settings[drum::airGain] = 12;
        require(measureTone(settings, 8000, sampleRate) > 0, "Supported sample rates must render nonzero finite audio");
    }
}

void testKickArchive()
{
    auto settings = drum::defaultSettings();
    settings[drum::bodyGain] = 5;
    settings[drum::bodyQ] = 2;
    settings[drum::attackQ] = 1.5f;
    settings[drum::output] = 6;
    settings[drum::multibandOn] = 1;
    settings[drum::lowThreshold] = -31;
    settings[drum::crossoverHigh] = 2400;
    settings[drum::instrument] = 9;
    settings[drum::glueOn] = settings[drum::ambienceOn] = 1;
    settings[drum::ambienceAmount] = 3;
    settings[drum::crushOn] = settings[drum::colourOn] = settings[drum::dynamicOn] = 1;
    settings[drum::autoStyleOption] = 1;
    settings[drum::autoIntensityOption] = 81;
    juce::MemoryOutputStream archive;
    archive.writeInt(0x44524133);
    archive.writeInt(2);
    archive.writeInt(drum::controlCount);
    require(drum::writeDrumEntry(archive, "kick-a", settings), "First archive entry must encode");
    settings[drum::bodyGain] = -3;
    require(drum::writeDrumEntry(archive, "kick-b", settings), "Second archive entry must encode");
    std::vector<drum::ArchivedDrum> entries;
    {
        juce::MemoryOutputStream previous43;
        previous43.writeInt(0x44524133);
        previous43.writeInt(1);
        previous43.writeInt(43);
        const juce::String id("previous-0.5");
        previous43.writeString(id);
        for (size_t control = 0; control < 43; ++control) previous43.writeFloat(settings[control]);
        juce::MemoryInputStream old43(previous43.getData(), previous43.getDataSize(), false);
        require(drum::readDrumArchive(old43, entries) && entries.size() == 1
            && entries[0].settings[drum::bodyQ] == 0 && entries[0].settings[drum::attackQ] == 0,
            "Previous 43-control archives must preserve their sound by inheriting the legacy global Q");
    }
    juce::MemoryInputStream input(archive.getData(), archive.getDataSize(), false);
    require(drum::readDrumArchive(input, entries) && entries.size() == 2
        && entries[0].id == "kick-a" && entries[0].settings[drum::bodyGain] == 5
        && entries[1].settings[drum::bodyGain] == -3 && entries[0].settings[drum::bodyQ] == 2
        && entries[0].settings[drum::attackQ] == 1.5f && entries[0].settings[drum::output] == 6,
        "Per-modification archive settings, independent Q and positive output must roundtrip independently");
    require(entries[0].settings[drum::multibandOn] == 1 && entries[0].settings[drum::lowThreshold] == -31
        && entries[0].settings[drum::crossoverHigh] == 2400, "ARA archives must persist multiband mode and its real parameters");
    require(entries[0].settings[drum::instrument] == 9 && entries[0].settings[drum::glueOn] == 1
        && entries[0].settings[drum::ambienceAmount] == 3, "ARA archives must preserve instrument, Glue and room dose");
    require(entries[0].settings[drum::crushOn] == 1 && entries[0].settings[drum::colourOn] == 1
        && entries[0].settings[drum::dynamicOn] == 1 && entries[0].settings[drum::autoStyleOption] == 1
        && entries[0].settings[drum::autoIntensityOption] == 81,
        "Finishing modules and Auto choices must survive per-event ARA archives");
    {
        juce::MemoryOutputStream previous88;
        previous88.writeInt(0x44524133);
        previous88.writeInt(1);
        previous88.writeInt(88);
        previous88.writeString("previous-0.8");
        for (size_t control = 0; control < 88; ++control) previous88.writeFloat(settings[control]);
        juce::MemoryInputStream old88(previous88.getData(), previous88.getDataSize(), false);
        require(drum::readDrumArchive(old88, entries) && entries.size() == 1
            && entries[0].settings[drum::instrument] == 9 && entries[0].settings[drum::glueOn] == 1
            && entries[0].settings[drum::crushOn] == 0 && entries[0].settings[drum::colourOn] == 0
            && entries[0].settings[drum::shapeOn] == 0 && entries[0].settings[drum::dynamicOn] == 0
            && entries[0].settings[drum::levelMatchOn] == 0,
            "Old 88-control archives must preserve bus settings with all finishing modules OFF");
    }
    {
        juce::MemoryOutputStream previous71;
        previous71.writeInt(0x44524133);
        previous71.writeInt(1);
        previous71.writeInt(71);
        previous71.writeString("previous-0.7");
        for (size_t control = 0; control < 71; ++control) previous71.writeFloat(settings[control]);
        juce::MemoryInputStream old71(previous71.getData(), previous71.getDataSize(), false);
        require(drum::readDrumArchive(old71, entries) && entries[0].settings[drum::instrument] == 0
            && entries[0].settings[drum::glueOn] == 0 && entries[0].settings[drum::ambienceOn] == 0,
            "Old 0.7 archives must remain Kick with new bus modules OFF");
    }
    {
        juce::MemoryOutputStream old47;
        old47.writeInt(0x44524133);
        old47.writeInt(1);
        old47.writeInt(47);
        old47.writeString("previous-0.6");
        for (size_t control = 0; control < 47; ++control) old47.writeFloat(settings[control]);
        juce::MemoryInputStream input47(old47.getData(), old47.getDataSize(), false);
        require(drum::readDrumArchive(input47, entries) && entries.size() == 1
            && entries[0].settings[drum::multibandOn] == 0,
            "Old 47-control archives must keep the legacy wideband compressor, not activate multiband");
    }
    for (size_t length = 0; length < archive.getDataSize(); ++length)
    {
        juce::MemoryInputStream truncated(archive.getData(), length, false);
        require(!drum::readDrumArchive(truncated, entries), "Every truncated archive must be rejected");
    }
    juce::MemoryOutputStream legacy;
    legacy.writeInt(0x44524131);
    juce::MemoryInputStream oldInput(legacy.getData(), legacy.getDataSize(), false);
    require(drum::readDrumArchive(oldInput, entries) && entries.empty(), "Legacy prototype archives must remain readable");
    juce::MemoryOutputStream previous;
    previous.writeInt(0x44524132);
    previous.writeInt(1);
    previous.writeString("kick-old");
    for (int control = 0; control < 29; ++control) previous.writeFloat(settings[static_cast<size_t>(control)]);
    juce::MemoryInputStream previousInput(previous.getData(), previous.getDataSize(), false);
    require(drum::readDrumArchive(previousInput, entries) && entries.size() == 1
        && entries[0].settings[drum::bodyGain] == -3 && entries[0].settings[drum::presenceOn] == 0
        && entries[0].settings[drum::focusOn] == 0 && entries[0].settings[drum::cleanupOn] == 0,
        "Old 29-control archives must restore unchanged with new processing disabled");
    juce::MemoryOutputStream invalid;
    invalid.writeInt(0x44524132);
    invalid.writeInt(10001);
    juce::MemoryInputStream invalidInput(invalid.getData(), invalid.getDataSize(), false);
    require(!drum::readDrumArchive(invalidInput, entries), "Archive object count must be bounded");
}
}

void testKickPresence()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        drum::KickPresenceDSP processor;
        processor.prepare(rate, 256, 2);
        drum::KickPresenceParameters parameters;
        parameters.thresholdDb = -32;
        parameters.bodyHz = 55;
        parameters.bodyAmount = 50;
        processor.configure(parameters);
        for (int sample = 0; sample < static_cast<int>(rate); ++sample)
        {
            const auto value = static_cast<float>(0.4 * std::sin(juce::MathConstants<double>::twoPi * 8000 * sample / rate));
            std::array<float, 2> signal { value, value * 0.5f };
            processor.detect(signal, 2);
            processor.process(signal, 2, 0, 1);
        }
        require(processor.hitCount() == 0, "High-frequency cymbal alone must not trigger the focused kick detector");
        processor.reset();
        double energy = 0, targetReal = 0, targetImaginary = 0, otherReal = 0, otherImaginary = 0;
        for (int sample = 0; sample < static_cast<int>(rate * 4); ++sample)
        {
            const auto time = static_cast<double>(sample) / rate;
            const auto age = std::fmod(time, 0.5);
            const auto value = static_cast<float>(0.6 * std::exp(-age * 40)
                * std::sin(juce::MathConstants<double>::twoPi * 80 * time));
            std::array<float, 2> signal { value, value * 0.5f };
            processor.detect(signal, 2);
            signal = { 0, 0 };
            processor.process(signal, 2, 0, 1);
            require(std::isfinite(signal[0]) && std::abs(signal[1] - signal[0] * 0.5f) < 0.00001f,
                "Triggered body must stay finite and retain the detected stereo relationship");
            energy += static_cast<double>(signal[0]) * signal[0];
            if (time < 0.22)
            {
                targetReal += signal[0] * std::cos(juce::MathConstants<double>::twoPi * 55 * time);
                targetImaginary += signal[0] * std::sin(juce::MathConstants<double>::twoPi * 55 * time);
                otherReal += signal[0] * std::cos(juce::MathConstants<double>::twoPi * 110 * time);
                otherImaginary += signal[0] * std::sin(juce::MathConstants<double>::twoPi * 110 * time);
            }
        }
        require(processor.hitCount() >= 7 && processor.hitCount() <= 8,
            "Repeated kicks must trigger one body per hit without retriggering on the decay");
        require(energy > 0.01 && targetReal * targetReal + targetImaginary * targetImaginary
            > 4 * (otherReal * otherReal + otherImaginary * otherImaginary),
            "Body reinforcement must generate measurable energy at the requested tuning rather than its octave");
        processor.reset();
        require(processor.hitCount() == 0 && processor.bodyMagnitude() == 0,
            "Seek reset must discard old detections and body tails");
        auto settings = drum::defaultSettings();
        settings[drum::focusOn] = settings[drum::cleanupOn] = 1;
        settings[drum::gateThreshold] = -25;
        settings[drum::cleanupDepth] = 18;
        require(measureTone(settings, 8000, rate) / measureTone(drum::defaultSettings(), 8000, rate) < 0.3f,
            "Selective cleanup must attenuate sustained cymbal-band bleed even with the broadband expander off");
        require(measureTone(settings, 80, rate) / measureTone(drum::defaultSettings(), 80, rate) > 0.95f,
            "Selective cleanup must preserve the low kick body when its detector is open");
        settings[drum::presenceOn] = 1;
        settings[drum::reinforceAmount] = 80;
        settings[drum::saturationDrive] = 8;
        settings[drum::bypass] = 1;
        require(std::abs(measureTone(settings, 80, rate) - measureTone(drum::defaultSettings(), 80, rate)) < 0.00001f,
            "Global bypass must bypass focused cleanup, body synthesis and saturation together");
    }
}

void testProposalEquivalence()
{
    enum class Signal { representative, highOnly, silence, nonFinite, clipped, tooShort, lowSampleRate,
        dense, hotPeaks, resonantBleed };
    int comparisons = 0;
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto analyser = std::make_unique<drum::DrumAnalyser>();
        {
            // The legacy reference is the arithmetic of profileVersion 2 (before the cymbal reference EQ).
            auto frozen = analyser->getPolicy();
            frozen.cymbalReference = false;
            analyser->setPolicy(frozen);
        }
        std::vector<float> audio(static_cast<size_t>(rate * 4));
        const auto compare = [&](const drum::Settings& original, drum::DrumStyle style, float dose)
        {
            const auto expected = drum::LegacyProposalReference(analyser->analysis()).propose(original, style, dose);
            const auto actual = analyser->propose(original, style, dose);
            ++comparisons;
            const auto context = juce::String("Proposal equivalence / rate ") + juce::String(rate)
                + " / role " + juce::String(original[drum::instrument])
                + " / style " + juce::String(static_cast<int>(style)) + " / dose " + juce::String(dose);
            const auto check = [&](bool equal, const juce::String& field)
            {
                if (!equal) throw std::runtime_error((context + " / " + field).toStdString());
            };
            check(actual.valid == expected.valid, "valid");
            check(actual.hits == expected.hits, "hits");
            const auto normalizedDose = std::isfinite(dose) ? juce::jlimit(0.0f, 1.0f, dose) : 0.0f;
            if (actual.valid && normalizedDose > 0.5f)
            {
                const auto baseline = drum::LegacyProposalReference(analyser->analysis()).propose(original, style, 0.5f);
                const auto factor = (0.5f + 2.5f * (normalizedDose - 0.5f)) / 0.5f;
                check(std::abs(actual.targetReduction - baseline.targetReduction * factor) < 0.00001f, "accentuated target");
                check(std::abs(actual.maximumReduction - baseline.maximumReduction * factor) < 0.00001f, "accentuated ceiling");
                for (size_t control = 0; control < actual.settings.size(); ++control)
                    check(actual.settings[control] >= drum::controlSpecs[control].minimum
                        && actual.settings[control] <= drum::controlSpecs[control].maximum, drum::controlSpecs[control].id);
                return actual;
            }
            check(actual.targetReduction == expected.targetReduction, "targetReduction");
            check(actual.maximumReduction == expected.maximumReduction, "maximumReduction");
            check(actual.bandTargets == expected.bandTargets, "bandTargets");
            check(actual.bandCeilings == expected.bandCeilings, "bandCeilings");
            check(actual.summary.startsWith(expected.summary), "historical summary");
            for (size_t control = 0; control < actual.settings.size(); ++control)
            {
                if (actual.valid && normalizedDose > 0 && (control >= drum::ambienceOn
                    || control == drum::cleanupOn || control == drum::cleanupDepth
                    || (control == drum::bodyFrequency && original[drum::instrument] >= 1 && original[drum::instrument] <= 5))) continue;
                if (actual.valid && normalizedDose > 0 && actual.settings[drum::cleanupOn] > 0.5f
                    && original[drum::instrument] >= 1 && original[drum::instrument] <= 5
                    && (control == drum::gateThreshold || control == drum::gateRatio || control == drum::gateRange
                        || control == drum::focusLow || control == drum::focusHigh
                        || control == drum::gateHold || control == drum::gateHysteresis)) continue;
                check(actual.settings[control] == expected.settings[control]
                    || (std::isnan(actual.settings[control]) && std::isnan(expected.settings[control])), drum::controlSpecs[control].id);
            }
            return actual;
        };
        for (const auto scenario : { Signal::representative, Signal::highOnly, Signal::silence, Signal::nonFinite,
            Signal::clipped, Signal::tooShort, Signal::lowSampleRate, Signal::dense, Signal::hotPeaks, Signal::resonantBleed })
        {
            for (size_t sample = 0; sample < audio.size(); ++sample)
            {
                const auto time = static_cast<double>(sample) / rate;
                const auto age = std::fmod(time, 0.5);
                const auto frequency = scenario == Signal::highOnly ? 4500.0 : 65.0;
                const auto amplitude = scenario == Signal::clipped ? 3.0 : scenario == Signal::hotPeaks ? 1.0 : 0.6;
                audio[sample] = scenario == Signal::silence ? 0.0f : scenario == Signal::dense ? 0.2f
                    : static_cast<float>(amplitude * std::exp(-age * 30)
                        * std::sin(juce::MathConstants<double>::twoPi * frequency * time));
                if (scenario == Signal::resonantBleed)
                    audio[sample] += static_cast<float>(0.03 * std::exp(-age * 4)
                        * std::sin(juce::MathConstants<double>::twoPi * 350 * time)
                        + 0.006 * std::sin(juce::MathConstants<double>::twoPi * 8000 * time));
            }
            if (scenario == Signal::nonFinite) audio[123] = std::numeric_limits<float>::infinity();
            analyser->prepare(scenario == Signal::lowSampleRate ? 7999 : rate);
            analyser->push(audio.data(), scenario == Signal::tooShort ? static_cast<int>(rate) : static_cast<int>(audio.size()));
            for (int role = 0; role < 10; ++role)
            {
                auto original = drum::defaultSettings();
                original[drum::instrument] = static_cast<float>(role);
                original[drum::bypass] = 1;
                original[drum::output] = role % 2 == 0 ? 6.0f : -12.0f;
                original[drum::input] = 5;
                original[drum::ambienceOn] = original[drum::presenceOn] = 1;
                for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
                    for (const auto dose : { 0.0f, 0.5f, 1.0f })
                    {
                        const auto proposal = compare(original, style, dose);
                        const auto valid = scenario == Signal::representative || scenario == Signal::hotPeaks
                            || scenario == Signal::resonantBleed || ((scenario == Signal::highOnly || scenario == Signal::dense) && role != 0);
                        require(proposal.valid == valid, "Explicit role validity must not require kick energy on other roles");
                        if (!valid)
                            require(proposal.settings == original, "Rejected proposals must preserve all original settings");
                        else
                        {
                            require(proposal.settings[drum::instrument] == role && proposal.settings[drum::bypass] == 1
                                && proposal.settings[drum::output] == juce::jmin(original[drum::output], -3.0f)
                                && proposal.settings[drum::input] == 0,
                                "Valid proposals must retain role/bypass/output semantics");
                            for (size_t control = 0; control < proposal.settings.size(); ++control)
                                require(std::isfinite(proposal.settings[control]) && proposal.settings[control] >= drum::controlSpecs[control].minimum
                                    && proposal.settings[control] <= drum::controlSpecs[control].maximum, "All Auto settings must remain finite and bounded");
                            if (dose == 0)
                                require(proposal.settings[drum::crushOn] + proposal.settings[drum::colourOn]
                                    + proposal.settings[drum::shapeOn] + proposal.settings[drum::dynamicOn]
                                    + proposal.settings[drum::clipOn] + proposal.settings[drum::levelMatchOn]
                                    + proposal.settings[drum::ambienceOn] == 0, "Zero intensity must disable all new audio modules");
                            else
                            {
                                require(proposal.settings[drum::levelMatchOn] == 1 && proposal.settings[drum::monitorMonoOn] == 0,
                                    "Auto must compare levels without forcing mono monitoring");
                                if (role == 6 || role == 7)
                                    require(proposal.settings[drum::crushOn] + proposal.settings[drum::colourOn]
                                        + proposal.settings[drum::shapeOn] + proposal.settings[drum::clipOn]
                                        + proposal.settings[drum::ambienceOn] == 0, "Cymbal roles must avoid automatic impact, distortion and room");
                                if (style == drum::DrumStyle::metal || role == 0 || role == 8)
                                    require(proposal.settings[drum::ambienceOn] == 0, "Auto room must follow the role and style policy");
                                const auto lighter = analyser->propose(original, style, dose * 0.5f);
                                require(proposal.settings[drum::crushAmount] >= lighter.settings[drum::crushAmount]
                                    && proposal.settings[drum::colourMix] >= lighter.settings[drum::colourMix]
                                    && proposal.settings[drum::dynamicLowRange] >= lighter.settings[drum::dynamicLowRange],
                                    "Intensity must increase finishing doses rather than just switching modules on");
                            }
                            if (role >= 2 && role <= 5)
                                require(proposal.settings[drum::gateRelease] == 350 && proposal.settings[drum::release] == 250
                                    && proposal.settings[drum::boxFrequency] == (style == drum::DrumStyle::natural ? 350 : 450),
                                    "Every tom role must use tom times and style-specific EQ");
                        }
                    }
                for (const auto dose : { -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity() })
                    compare(original, static_cast<drum::DrumStyle>(99), dose);
            }
            for (const auto role : { -1.0f, 99.0f, 1.49f, 1.5f, -0.5f,
                std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
            {
                auto original = drum::defaultSettings();
                original[drum::instrument] = role;
                compare(original, drum::DrumStyle::rock, 0.65f);
            }
        }
    }
    require(comparisons == 4110, "Equivalence must cover every signal/rate/role/style/dose and sanitization case");
    std::cout << "PASS: " << comparisons << " proposal checks: historical core, adaptive finishes and intensity\n";
}

void testAdaptiveFinishing()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto analyser = std::make_unique<drum::DrumAnalyser>();
        std::vector<float> source(static_cast<size_t>(rate * 3.2));
        for (size_t sample = 0; sample < source.size(); ++sample)
        {
            const auto time = sample / rate;
            const auto age = std::fmod(time, 0.4);
            const auto velocity = static_cast<int>(time / 0.4) % 2 == 0 ? 1.0 : 0.35;
            source[sample] = static_cast<float>(0.25 * velocity * std::exp(-age * 25)
                * (0.75 * std::sin(juce::MathConstants<double>::twoPi * 65 * time)
                    + 0.6 * std::sin(juce::MathConstants<double>::twoPi * 150 * time)
                    + 0.25 * std::sin(juce::MathConstants<double>::twoPi * 650 * time)
                    + 0.3 * std::sin(juce::MathConstants<double>::twoPi * 7000 * time))
                + 0.003 * std::sin(juce::MathConstants<double>::twoPi * 9000 * time));
        }
        analyser->prepare(rate);
        analyser->push(source.data(), static_cast<int>(source.size()));
        require(analyser->analysis().peakDb() < -6, "Finishing fixture must exercise a source below the old clip activation threshold");
        for (int role = 0; role < 10; ++role)
            for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
            {
                auto original = drum::defaultSettings();
                original[drum::instrument] = static_cast<float>(role);
                auto previous = analyser->propose(original, style, 0);
                for (const auto intensity : { 0.2f, 0.5f, 0.8f, 1.0f })
                {
                    const auto current = analyser->propose(original, style, intensity);
                    require(current.valid, "Role-aware finishing proposals must accept the moderate-level percussion fixture");
                    if (previous.settings[drum::levelMatchOn] > 0.5f)
                        for (const auto control : { drum::crushAmount, drum::colourDrive, drum::colourMix,
                            drum::shapeAttack, drum::dynamicLowRange, drum::dynamicHighRange, drum::clipMix, drum::cleanupDepth })
                            require(current.settings[control] + 0.00001f >= previous.settings[control],
                                "Finishing amounts must increase monotonically with intensity");
                    if (role <= 5)
                    {
                        require(current.settings[drum::crushOn] == 1 && current.settings[drum::colourOn] == 1
                            && current.settings[drum::shapeOn] == 1 && current.settings[drum::dynamicOn] == 1
                            && current.settings[drum::cleanupOn] == 1,
                            "A measured close-mic performance must engage the complete useful finishing chain");
                        if (role > 0)
                            require(current.settings[drum::autoMeasuredOn] == 1 && current.settings[drum::presenceOn] == 0,
                                "Measured Snare/Tom bodies must not enable synthetic Kick reinforcement");
                        if (intensity == 1) require(current.settings[drum::clipOn] == 1,
                            "High intensity must use gentle soft clipping without requiring near-zero source peaks");
                    }
                    if (role == 6 || role == 7)
                        require(current.settings[drum::cleanupOn] + current.settings[drum::crushOn]
                            + current.settings[drum::colourOn] + current.settings[drum::clipOn] == 0,
                            "Cymbal roles must retain tails and avoid automatic close-mic cleanup or distortion");
                    previous = current;
                }
            }
        auto original = drum::defaultSettings();
        original[drum::instrument] = 1;
        auto candidate = analyser->propose(original, drum::DrumStyle::rock, 1);
        require(std::abs(candidate.settings[drum::bodyFrequency] - 150) < 15,
            "Snare measured body must track the fixture rather than its default 180 Hz");
        auto preview = std::make_unique<drum::DrumPresetPreview>();
        juce::AudioBuffer<float> audio(1, 256);
        for (int pass = 0; pass < 2; ++pass)
        {
            preview->prepare(rate, 1, candidate);
            for (size_t offset = 0; offset < source.size(); offset += 256)
            {
                const auto count = static_cast<int>(juce::jmin(size_t { 256 }, source.size() - offset));
                audio.copyFrom(0, 0, source.data() + offset, count);
                preview->push(audio, count);
            }
            if (pass == 0) preview->refineThreshold(candidate); else preview->finish(candidate);
        }
        drum::DrumEngine engine;
        engine.prepare(rate, 256, 1);
        std::array<float, drum::finishMeterCount> maximums {};
        float peak = 0;
        for (size_t offset = 0; offset < source.size(); offset += 256)
        {
            const auto count = static_cast<int>(juce::jmin(size_t { 256 }, source.size() - offset));
            audio.copyFrom(0, 0, source.data() + offset, count);
            juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), 1, count);
            engine.process(block, candidate.settings);
            peak = juce::jmax(peak, block.getMagnitude(0, count));
            for (size_t meter = 0; meter < maximums.size(); ++meter)
                maximums[meter] = juce::jmax(maximums[meter], std::abs(engine.finishMeters[meter].load()));
        }
        require(peak <= juce::Decibels::decibelsToGain(-0.95f), "Stronger adaptive finishes must retain verified sample headroom");
        require(maximums[0] > 1 && maximums[1] + maximums[2] > 0.5f && maximums[5] > 0.25f,
            "Crush, dynamic EQ and Attack/Sustain must actually act in the calibrated high-intensity preset");
        require(maximums[6] > 0.00001f && maximums[7] > 0.00001f && maximums[12] > 1,
            "Colour, soft clip and Bleed Clean must produce measurable processing rather than decorative ON states");
        require(candidate.summary.contains("Bleed Clean max"), "Calibrated Snare Bilan must report the measured cleanup rather than claim it is disabled");
    }
}

void testAutoPreset()
{
    testAdaptiveFinishing();
    testProposalEquivalence();
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        drum::DrumAnalyser analyser;
        analyser.prepare(rate);
        std::vector<float> audio(static_cast<size_t>(rate * 4));
        analyser.push(audio.data(), static_cast<int>(audio.size()));
        require(!analyser.propose(drum::defaultSettings()).valid, "Silence must not produce an auto preset");
        for (size_t index = 0; index < audio.size(); ++index)
        {
            const auto time = static_cast<double>(index) / rate;
            const auto age = std::fmod(time, 0.5);
            audio[index] = static_cast<float>(0.6 * std::exp(-age * 30) * std::sin(juce::MathConstants<double>::twoPi * 65 * time));
        }
        analyser.prepare(rate);
        for (size_t offset = 0; offset < audio.size(); offset += 257)
            analyser.push(audio.data() + offset, static_cast<int>(juce::jmin(size_t { 257 }, audio.size() - offset)));
        const auto proposal = analyser.propose(drum::defaultSettings());
        require(proposal.valid && proposal.hits >= 3, "A repeated decaying kick must produce a proposal");
        require(std::abs(proposal.settings[drum::bodyFrequency] - 65) < 20, "Analysis must locate kick body");
        require(proposal.settings[drum::gateRange] <= 10.5f && proposal.settings[drum::makeup] == 0
            && proposal.settings[drum::input] == 0 && proposal.settings[drum::output] <= -3,
            "Full intensity must keep bounded dynamics and no initial gain boost");
        for (size_t index = 0; index < proposal.settings.size(); ++index)
            require(std::isfinite(proposal.settings[index]) && proposal.settings[index] >= drum::controlSpecs[index].minimum
                && proposal.settings[index] <= drum::controlSpecs[index].maximum, "Every proposed parameter must be in range");
        analyser.prepare(rate);
        analyser.push(audio.data(), static_cast<int>(audio.size()));
        require(analyser.propose(drum::defaultSettings()).settings == proposal.settings,
            "Analysis must be independent of incoming block boundaries");
        const auto rock = analyser.propose(drum::defaultSettings(), drum::DrumStyle::rock);
        for (int element = 0; element < 10; ++element)
        {
            auto original = drum::defaultSettings();
            original[drum::instrument] = static_cast<float>(element);
            const auto gentle = analyser.propose(original, drum::DrumStyle::rock, 0.5f);
            const auto full = analyser.propose(original, drum::DrumStyle::rock, 1.0f);
            require(gentle.valid && full.valid
                && std::abs(full.targetReduction - gentle.targetReduction * 3.5f) < 0.00001f
                && std::abs(full.maximumReduction - gentle.maximumReduction * 3.5f) < 0.00001f,
                "All ten roles must have a stronger high-intensity target and ceiling");
            require(full.summary.contains("100%") && !full.summary.contains("175%"),
                "Reports must show the user-facing intensity, not its internal strength");
        }
        for (int element = 1; element < 10; ++element)
        {
            auto original = drum::defaultSettings();
            original[drum::instrument] = static_cast<float>(element);
            const auto profile = analyser.propose(original, drum::DrumStyle::rock);
            require(profile.valid && profile.settings[drum::instrument] == element,
                "Every selected drum role must route to its own valid proposal and persist the role");
            require(profile.settings[drum::focusOn] == 0 && profile.settings[drum::presenceOn] == 0,
                "Non-kick profiles must never enable synthetic Kick processing");
            if (element >= 6) require(profile.settings[drum::cleanupOn] == 0,
                "Distant microphones and cymbals must not receive automatic Bleed Clean");
            if (element >= 6) require(profile.settings[drum::gateOn] == 0,
                "Hi-hat, overheads, room and master must preserve tails without automatic gating");
            require(analyser.propose(original, drum::DrumStyle::rock, 0).settings[drum::compressorOn] == 0,
                "Zero intensity must disable proposed dynamics on all roles");
        }
        const auto metal = analyser.propose(drum::defaultSettings(), drum::DrumStyle::metal);
        const auto half = analyser.propose(drum::defaultSettings(), drum::DrumStyle::rock, 0.5f);
        const auto zero = analyser.propose(drum::defaultSettings(), drum::DrumStyle::metal, 0);
        require(proposal.settings[drum::eqQ] == 0.85f && rock.settings[drum::eqQ] == 1.0f
            && metal.settings[drum::eqQ] == 1.2f, "Auto EQ bands must use the requested tighter profile Q");
        require(rock.valid && metal.valid && rock.settings[drum::bodyGain] > 0 && rock.settings[drum::boxGain] < 0
            && metal.settings[drum::attackGain] > rock.settings[drum::attackGain], "Styles must offer explicit distinct musical shaping");
        require(std::abs(half.settings[drum::bodyGain] * 3.5f - rock.settings[drum::bodyGain]) < 0.00001f
            && zero.settings[drum::eqOn] == 0 && zero.settings[drum::compressorOn] == 0,
            "Intensity must scale musical EQ and disable dynamics at zero");
        drum::DrumEngine calibrated;
        calibrated.prepare(rate, 257, 1);
        juce::AudioBuffer<float> rendered(1, 257);
        float reduction = 0;
        for (size_t offset = 0; offset < audio.size(); offset += 257)
        {
            const auto count = static_cast<int>(juce::jmin(size_t { 257 }, audio.size() - offset));
            rendered.clear();
            rendered.copyFrom(0, 0, audio.data() + offset, count);
            auto legacySettings = rock.settings;
            legacySettings[drum::multibandOn] = 0;
            calibrated.process(rendered, legacySettings);
            reduction = juce::jmax(reduction, calibrated.compressionMeter.load());
        }
        require(reduction >= 1 && reduction <= rock.maximumReduction + 0.5f,
            "Envelope-calibrated Rock compression must engage within the accentuated ceiling");
        for (const bool presenceEnabled : { false, true })
        for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
        {
            auto candidate = analyser.propose(drum::defaultSettings(), style);
            candidate.settings[drum::multibandOn] = 0;
            if (!presenceEnabled)
                candidate.settings[drum::focusOn] = candidate.settings[drum::cleanupOn] = candidate.settings[drum::presenceOn] = 0;
            drum::DrumPresetPreview preview;
            for (int pass = 0; pass < 2; ++pass)
            {
                preview.prepare(rate, 1, candidate);
                for (size_t offset = 0; offset < audio.size(); offset += 257)
                {
                    const auto count = static_cast<int>(juce::jmin(size_t { 257 }, audio.size() - offset));
                    rendered.copyFrom(0, 0, audio.data() + offset, count);
                    preview.push(rendered, count);
                }
                if (pass == 0) preview.refineThreshold(candidate); else preview.finish(candidate);
            }
            calibrated.prepare(rate, 257, 1);
            reduction = 0;
            float peak = 0;
            double dryEnergy = 0, wetEnergy = 0;
            for (size_t offset = 0; offset < audio.size(); offset += 257)
            {
                const auto count = static_cast<int>(juce::jmin(size_t { 257 }, audio.size() - offset));
                rendered.copyFrom(0, 0, audio.data() + offset, count);
                juce::AudioBuffer<float> block(rendered.getArrayOfWritePointers(), 1, count);
                calibrated.process(block, candidate.settings);
                reduction = juce::jmax(reduction, calibrated.compressionMeter.load());
                peak = juce::jmax(peak, block.getMagnitude(0, count));
                for (int sample = 0; sample < count; ++sample)
                {
                    const auto dry = audio[offset + static_cast<size_t>(sample)];
                    const auto wet = block.getSample(0, sample);
                    dryEnergy += static_cast<double>(dry) * dry;
                    wetEnergy += static_cast<double>(wet) * wet;
                }
            }
            require(candidate.valid && reduction > candidate.targetReduction - 0.2f && reduction <= candidate.maximumReduction + 0.15f,
                "Active-window calibration must engage while bounding the strongest hit in the real DSP");
            require(peak <= juce::Decibels::decibelsToGain(-0.95f),
                "Preview output trim must respect measured sample peak headroom");
            require(candidate.settings[drum::output] >= -60 && candidate.settings[drum::output] <= 12,
                "Final compensation must remain within the existing output gain range");
            if (presenceEnabled)
            {
                require(calibrated.detectedHits.load() >= 7 && calibrated.detectedHits.load() <= 8,
                    "Full-chain Auto presence must trigger once on every synthetic kick");
                require(candidate.summary.contains("ACTUAL VERIFICATION") && candidate.summary.contains("kickReinforceHz"),
                    "Bilan must include real render measurements and final new parameter values");
            }
            if (style != drum::DrumStyle::natural)
                require(std::abs(10 * std::log10(wetEnergy / dryEnergy)) < 0.05,
                    "Full-wet makeup compensation must match RMS when sample peak headroom permits");
        }
        auto outlierAudio = audio;
        for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
        {
            auto candidate = analyser.propose(drum::defaultSettings(), style);
            require(candidate.settings[drum::multibandOn] == 1, "Valid Auto proposal must configure multiband compression");
            auto preview = std::make_unique<drum::DrumPresetPreview>();
            for (int pass = 0; pass < 2; ++pass)
            {
                preview->prepare(rate, 1, candidate);
                for (size_t offset = 0; offset < audio.size(); offset += 257)
                {
                    const auto count = static_cast<int>(juce::jmin(size_t { 257 }, audio.size() - offset));
                    rendered.copyFrom(0, 0, audio.data() + offset, count);
                    preview->push(rendered, count);
                }
                if (pass == 0) preview->refineThreshold(candidate);
                else
                {
                    preview->finish(candidate);
                    for (size_t band = 0; band < 3; ++band)
                        if (candidate.settings[static_cast<size_t>(drum::lowRatio) + band * 7] > 1)
                            if (std::abs(preview->bandActiveReduction(band) - candidate.bandTargets[band]) > 0.45f)
                                throw std::runtime_error(("Multiband P75 at " + juce::String(rate) + " Hz / "
                                    + drum::drumStyleName(style) + " / band " + juce::String(static_cast<int>(band))
                                    + " / measured " + juce::String(preview->bandActiveReduction(band))
                                    + " / target " + juce::String(candidate.bandTargets[band])
                                    + " / threshold " + juce::String(candidate.settings[static_cast<size_t>(drum::lowThreshold) + band * 7])).toStdString());
                }
            }
            require(candidate.valid && candidate.summary.contains("MULTIBAND COMPRESSION"),
                "Auto Bilan must include actual per-band calibration measurements");
            calibrated.prepare(rate, 257, 1);
            std::array<float, 3> maximums {};
            double dryEnergy = 0, wetEnergy = 0;
            float peak = 0;
            for (size_t offset = 0; offset < audio.size(); offset += 257)
            {
                const auto count = static_cast<int>(juce::jmin(size_t { 257 }, audio.size() - offset));
                rendered.copyFrom(0, 0, audio.data() + offset, count);
                juce::AudioBuffer<float> block(rendered.getArrayOfWritePointers(), 1, count);
                calibrated.process(block, candidate.settings);
                peak = juce::jmax(peak, block.getMagnitude(0, count));
                for (size_t band = 0; band < 3; ++band)
                    maximums[band] = juce::jmax(maximums[band], calibrated.bandReductionMeters[band].load());
                for (int sample = 0; sample < count; ++sample)
                {
                    const auto dry = audio[offset + static_cast<size_t>(sample)];
                    const auto wet = block.getSample(0, sample);
                    require(std::isfinite(wet), "Auto multiband render must remain finite");
                    dryEnergy += dry * dry;
                    wetEnergy += wet * wet;
                }
            }
            for (size_t band = 0; band < 3; ++band)
                require(maximums[band] <= candidate.bandCeilings[band] + 0.01f,
                    "Every Auto multiband compressor must respect its real reduction ceiling");
            require(maximums[0] > 0.5f && peak <= juce::Decibels::decibelsToGain(-0.95f),
                "Auto low-band compression must engage while preserving sample headroom");
            if (style != drum::DrumStyle::natural)
                require(std::abs(10 * std::log10(wetEnergy / dryEnergy)) < 0.08,
                    "Full-wet Auto multiband compensation must match source RMS when headroom permits");
        }
        for (size_t index = static_cast<size_t>(rate * 0.5); index < outlierAudio.size(); ++index)
            outlierAudio[index] *= 0.4f;
        analyser.prepare(rate);
        analyser.push(outlierAudio.data(), static_cast<int>(outlierAudio.size()));
        auto bounded = analyser.propose(drum::defaultSettings(), drum::DrumStyle::rock);
        bounded.settings[drum::multibandOn] = 0;
        drum::DrumPresetPreview outlierPreview;
        for (int pass = 0; pass < 2; ++pass)
        {
            outlierPreview.prepare(rate, 1, bounded);
            for (size_t offset = 0; offset < outlierAudio.size(); offset += 257)
            {
                const auto count = static_cast<int>(juce::jmin(size_t { 257 }, outlierAudio.size() - offset));
                rendered.copyFrom(0, 0, outlierAudio.data() + offset, count);
                outlierPreview.push(rendered, count);
            }
            if (pass == 0) outlierPreview.refineThreshold(bounded); else outlierPreview.finish(bounded);
        }
        calibrated.prepare(rate, 257, 1);
        float normalReduction = 0, strongestReduction = 0;
        for (size_t offset = 0; offset < outlierAudio.size(); offset += 257)
        {
            const auto count = static_cast<int>(juce::jmin(size_t { 257 }, outlierAudio.size() - offset));
            rendered.copyFrom(0, 0, outlierAudio.data() + offset, count);
            juce::AudioBuffer<float> block(rendered.getArrayOfWritePointers(), 1, count);
            calibrated.process(block, bounded.settings);
            strongestReduction = juce::jmax(strongestReduction, calibrated.compressionMeter.load());
            if (offset >= static_cast<size_t>(rate))
                normalReduction = juce::jmax(normalReduction, calibrated.compressionMeter.load());
        }
        require(bounded.valid && normalReduction > 1 && strongestReduction <= bounded.maximumReduction + 0.15f,
            "One hot outlier must not disable normal-hit compression or exceed the calibration ceiling");
        for (size_t index = 0; index < audio.size(); ++index)
            audio[index] = static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi * 65 * index / rate));
        analyser.prepare(rate);
        analyser.push(audio.data(), static_cast<int>(audio.size()));
        require(!analyser.propose(drum::defaultSettings()).valid, "A steady tone is not a kick performance");
        for (size_t index = 0; index < audio.size(); ++index)
        {
            const auto time = static_cast<double>(index) / rate;
            audio[index] = static_cast<float>(0.3 * std::exp(-std::fmod(time, 0.25) * 40)
                * std::sin(juce::MathConstants<double>::twoPi * 4500 * time));
        }
        analyser.prepare(rate);
        analyser.push(audio.data(), static_cast<int>(audio.size()));
        require(!analyser.propose(drum::defaultSettings()).valid, "High-only percussion must not be mistaken for a kick");
        auto snare = drum::defaultSettings();
        snare[drum::instrument] = static_cast<float>(drum::DrumInstrument::snare);
        require(analyser.propose(snare).valid, "Explicit Snare must analyse percussion without kick-band energy");
        snare[drum::instrument] = static_cast<float>(drum::DrumInstrument::overheads);
        const auto overhead = analyser.propose(snare, drum::DrumStyle::rock);
        const auto overheadNatural = analyser.propose(snare, drum::DrumStyle::natural);
        require(overhead.valid && overhead.settings[drum::gateOn] == 0 && overheadNatural.valid
            && overheadNatural.settings[drum::gateOn] == 0 && overheadNatural.settings[drum::eqOn] == 0,
            "Overheads must analyse cymbals without imposing kick gating, and Natural without preset boosts");
        require(overhead.settings[drum::eqOn] == 1 && overhead.settings[drum::highPass] == 180,
            "Rock Overheads carry the reference EQ (profileVersion 3)");
        audio[123] = std::numeric_limits<float>::quiet_NaN();
        analyser.prepare(rate);
        analyser.push(audio.data(), static_cast<int>(audio.size()));
        require(!analyser.propose(drum::defaultSettings()).valid, "Non-finite audio must be rejected");
    }
}

void testAutoExpander()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<float> source(static_cast<size_t>(rate * 6));
        for (size_t sample = 0; sample < source.size(); ++sample)
        {
            const auto time = static_cast<double>(sample) / rate;
            const auto age = std::fmod(time, 0.5);
            const auto velocity = static_cast<int>(time * 2) % 2 == 0 ? 0.6 : 0.18;
            source[sample] = static_cast<float>(velocity * std::exp(-age * 30)
                * std::sin(juce::MathConstants<double>::twoPi * 65 * time)
                + 0.004 * std::sin(juce::MathConstants<double>::twoPi * 3000 * time));
        }
        drum::DrumAnalyser analyser;
        analyser.prepare(rate);
        for (size_t offset = 0; offset < source.size(); offset += 257)
            analyser.push(source.data() + offset, static_cast<int>(juce::jmin(size_t { 257 }, source.size() - offset)));
        for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
        {
            const auto proposal = analyser.propose(drum::defaultSettings(), style);
            require(proposal.valid && proposal.settings[drum::gateOn] == 1,
                "Alternating kicks with separable bleed must enable expansion");
            auto settings = proposal.settings;
            settings[drum::filtersOn] = settings[drum::eqOn] = settings[drum::compressorOn] = 0;
            settings[drum::output] = 0;
            auto referenceSettings = settings;
            referenceSettings[drum::gateOn] = 0;
            drum::DrumEngine engine, reference;
            engine.prepare(rate, 256, 1);
            reference.prepare(rate, 256, 1);
            juce::AudioBuffer<float> wet(1, 256), dry(1, 256);
            double quietDry = 0, quietWet = 0, weakDry = 0, weakWet = 0;
            for (size_t offset = 0; offset < source.size(); offset += 256)
            {
                const auto count = static_cast<int>(juce::jmin(size_t { 256 }, source.size() - offset));
                wet.copyFrom(0, 0, source.data() + offset, count);
                dry.copyFrom(0, 0, source.data() + offset, count);
                juce::AudioBuffer<float> wetBlock(wet.getArrayOfWritePointers(), 1, count);
                juce::AudioBuffer<float> dryBlock(dry.getArrayOfWritePointers(), 1, count);
                engine.process(wetBlock, settings);
                reference.process(dryBlock, referenceSettings);
                require(engine.gateMeter.load() <= settings[drum::gateRange] + 0.001f,
                    "Auto expansion must stay bounded by its range");
                for (int sample = 0; sample < count; ++sample)
                {
                    const auto time = static_cast<double>(offset + static_cast<size_t>(sample)) / rate;
                    const auto age = std::fmod(time, 0.5);
                    const auto dryValue = static_cast<double>(dry.getSample(0, sample));
                    const auto wetValue = static_cast<double>(wet.getSample(0, sample));
                    if (time >= 0.5 && age > 0.3)
                    {
                        quietDry += dryValue * dryValue;
                        quietWet += wetValue * wetValue;
                    }
                    if (static_cast<int>(time * 2) % 2 == 1 && age < 0.07)
                    {
                        weakDry += dryValue * dryValue;
                        weakWet += wetValue * wetValue;
                    }
                }
            }
            require(10 * std::log10(quietWet / quietDry) < -2,
                "Auto expansion must actually attenuate audible bleed between kicks");
            require(10 * std::log10(weakWet / weakDry) > -1,
                "Auto expansion must preserve the first seventy milliseconds of weak kicks within one dB");
            require(analyser.propose(drum::defaultSettings(), style, 0).settings[drum::gateOn] == 0,
                "Zero intensity must disable expansion");
        }
        for (size_t sample = 0; sample < source.size(); ++sample)
        {
            const auto time = static_cast<double>(sample) / rate;
            const auto age = std::fmod(time, 0.125);
            source[sample] = static_cast<float>(0.6 * std::exp(-age * 30)
                * std::sin(juce::MathConstants<double>::twoPi * 65 * time)
                + 0.2 * std::sin(juce::MathConstants<double>::twoPi * 3000 * time));
        }
        analyser.prepare(rate);
        analyser.push(source.data(), static_cast<int>(source.size()));
        const auto dense = analyser.propose(drum::defaultSettings(), drum::DrumStyle::rock);
        if (!(dense.valid && dense.settings[drum::gateOn] == 0 && dense.summary.contains("insufficient contrast")))
            throw std::runtime_error(("Dense expansion test at " + juce::String(rate, 0)
                + " Hz: " + dense.summary).toStdString());
        drum::DrumEngine display;
        display.prepare(rate, 256, 1);
        auto settings = drum::defaultSettings();
        settings[drum::gateOn] = 1;
        settings[drum::gateThreshold] = -12;
        settings[drum::gateRange] = 8;
        juce::AudioBuffer<float> silence(1, 256);
        silence.clear();
        display.process(silence, settings);
        const auto held = display.gateDisplayMeter.load();
        settings[drum::gateOn] = 0;
        for (int block = 0; block < 10; ++block) display.process(silence, settings);
        require(display.gateMeter.load() == 0 && display.gateDisplayMeter.load() > held * 0.6f,
            "EXP display release must preserve brief readings independently of the raw meter");
        for (int block = 0; block < 300; ++block) display.process(silence, settings);
        require(display.gateDisplayMeter.load() < 0.1f, "EXP display must return to zero when disabled");
        display.reset();
        require(display.gateDisplayMeter.load() == 0, "Seek reset must clear EXP display memory");
    }
}

void testTemporalAutoEQ()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto analyse = [rate](bool resonance, bool persistentTreble, bool attackOnly, float gain, double trebleAmplitude = 0.06)
        {
            drum::DrumAnalyser analyser;
            analyser.prepare(rate);
            std::array<float, 257> block {};
            const auto length = static_cast<int>(rate * 5);
            for (int offset = 0; offset < length; offset += static_cast<int>(block.size()))
            {
                const auto count = juce::jmin(static_cast<int>(block.size()), length - offset);
                for (int index = 0; index < count; ++index)
                {
                    const auto time = static_cast<double>(offset + index) / rate;
                    const auto age = std::fmod(time, 0.5);
                    const auto velocity = static_cast<int>(time * 2) % 2 == 0 ? 1.0 : 0.4;
                    auto value = velocity * 0.6 * std::exp(-age * 30)
                        * std::sin(juce::MathConstants<double>::twoPi * 65 * time);
                    if (resonance)
                        value += velocity * 0.16 * std::exp(-age * 12)
                            * std::sin(juce::MathConstants<double>::twoPi * 400 * time);
                    if (persistentTreble || attackOnly)
                        value += trebleAmplitude * (attackOnly ? std::exp(-age * 150) : 1.0)
                            * std::sin(juce::MathConstants<double>::twoPi * 8000 * time);
                    block[static_cast<size_t>(index)] = static_cast<float>(value * gain);
                }
                analyser.push(block.data(), count);
            }
            return analyser.propose(drum::defaultSettings());
        };
        const auto clean = analyse(false, false, false, 1);
        require(clean.valid && clean.settings[drum::eqOn] == 0 && clean.settings[drum::boxGain] == 0,
            "Clean decaying kicks must remain neutral rather than receive an invented EQ");
        require(clean.summary.contains("Neutral EQ"), "Neutral EQ must be explicitly explained");
        const auto ringing = analyse(true, false, false, 1);
        require(ringing.valid && ringing.settings[drum::boxGain] < -0.1f,
            "A persistent low-mid resonance must receive a measured corrective cut");
        require(std::abs(ringing.settings[drum::boxFrequency] - 400) < 70,
            "Corrective EQ must target the measured resonance rather than a fixed 350 Hz");
        require(ringing.settings[drum::boxGain] >= -3 && ringing.summary.contains("Resonance"),
            "Summary must identify the bounded correction actually applied");
        const auto quiet = analyse(true, false, false, 0.2f);
        require(quiet.valid && std::abs(quiet.settings[drum::boxGain] - ringing.settings[drum::boxGain]) < 0.05f,
            "Resonance correction must be invariant to overall input gain");
        const auto bleed = analyse(false, true, false, 1);
        require(bleed.valid && bleed.settings[drum::airGain] < -0.1f && bleed.summary.contains("Persistent high-frequency bleed"),
            "Persistent high-frequency bleed must be distinguished from a clean kick");
        const auto click = analyse(false, false, true, 1);
        require(click.valid && click.settings[drum::airGain] == 0,
            "Useful high-frequency attack must not be mistaken for persistent bleed");
        const auto discreetBleed = analyse(false, true, false, 1, 0.01);
        require(discreetBleed.valid && discreetBleed.settings[drum::airGain] == 0,
            "Low-level persistent treble must not force an unnecessary EQ cut");
        require(clean.hits >= 8 && clean.hits <= 10, "Alternating strong and weak kicks must both be detected");
    }
}

void testAutoLiveCapture()
{
    const auto makeBlock = [](juce::AudioBuffer<float>& audio, int block) {
        for (int sample = 0; sample < 256; ++sample)
        {
            const auto time = static_cast<double>(block * 256 + sample) / 48000;
            audio.setSample(0, sample, static_cast<float>(0.6 * std::exp(-std::fmod(time, 0.5) * 30)
                * std::sin(juce::MathConstants<double>::twoPi * 65 * time)));
        }
    };
    const auto run = [&](bool live, bool& sourceBeforeCaptureEnd) {
        ZeroEffortDrumProcessor processor;
        processor.setRateAndBufferSizeDetails(48000, 256);
        processor.prepareToPlay(48000, 256);
        processor.setAutoStyle(drum::DrumStyle::natural, 0.8f);
        processor.startAutoPreset();
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> audio(1, 256);
        sourceBeforeCaptureEnd = false;
        for (int block = 0; block < 1500; ++block)
        {
            makeBlock(audio, block);
            processor.processBlock(audio, midi);
            if (live && block % 20 == 0 && block < 1400)
            {
                processor.updateAutoPreset();
                if (block > 400 && processor.getAutoRadar().source.any()) sourceBeforeCaptureEnd = true;
            }
        }
        for (int step = 0; step < 200 && processor.autoPresetBusy(); ++step) processor.updateAutoPreset();
        require(!processor.autoPresetBusy(), "Insert Auto must finish after the capture");
        return processor.getSettings();
    };
    bool liveSource = false, lateSource = false;
    const auto live = run(true, liveSource);
    const auto late = run(false, lateSource);
    require(liveSource && !lateSource, "Insert Auto must draw the source curve while the capture is still running");
    for (size_t index = 0; index < live.size(); ++index)
        require(live[index] == late[index], "Analysing during the capture must give exactly the same proposal");
}

void testAutoWorkflow()
{
    {
        ZeroEffortDrumProcessor fresh;
        require(fresh.getAutoStyle() == drum::DrumStyle::rock && std::abs(fresh.getAutoIntensity() - 0.75f) < 0.00001f,
            "A new instance must start on Rock at 75 %");
        require(fresh.getSettings()[drum::autoStyleOption] == 1 && fresh.getSettings()[drum::autoIntensityOption] == 75,
            "The Auto options must agree with the session defaults");
    }
    ZeroEffortDrumProcessor processor;
    processor.setRateAndBufferSizeDetails(48000, 256);
    processor.prepareToPlay(48000, 256);
    processor.setControl(drum::bodyGain, 4.5f);
    processor.setControl(drum::input, -6);
    processor.setAutoStyle(drum::DrumStyle::rock, 0.65f);
    const auto before = processor.getSettings();
    require(!processor.applyAutoPreset() && !processor.revertAutoPreset(), "No proposal means no apply or revert");
    processor.startAutoPreset();
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(1, 256);
    for (int block = 0; block < 1500; ++block)
    {
        for (int sample = 0; sample < 256; ++sample)
        {
            const auto time = static_cast<double>(block * 256 + sample) / 48000;
            audio.setSample(0, sample, static_cast<float>(0.6 * std::exp(-std::fmod(time, 0.5) * 30)
                * std::sin(juce::MathConstants<double>::twoPi * 65 * time)));
        }
        processor.processBlock(audio, midi);
        if (block % 50 == 0) processor.updateAutoPreset();
    }
    for (int step = 0; step < 80 && processor.autoPresetBusy(); ++step) processor.updateAutoPreset();
    require(!processor.autoPresetBusy() && !processor.canApplyAutoPreset() && processor.canRevertAutoPreset(),
        "Successful analysis must automatically apply the verified proposal and offer rollback");
    require(processor.getSettings() != before, "Completed Auto must activate the selected processing");
    require(processor.getAutoStyle() == drum::DrumStyle::rock && processor.getAutoIntensity() == 0.65f,
        "Profile selection must survive analysis without changing manual controls");
    require(processor.getSettings()[drum::levelMatchOn] == 1 && processor.getSettings()[drum::crushOn] == 1,
        "Automatic application must include adaptive finishing controls");
    const auto applied = processor.getSettings();
    require(applied != before && applied[drum::output] <= 12, "Applied calibrated preset must reach actual processing parameters");
    require(processor.revertAutoPreset(), "Rollback must remain available for an unmodified applied proposal");
    for (size_t index = 0; index < before.size(); ++index)
        require(std::abs(processor.getSettings()[index] - before[index]) < 0.005f,
            "Rollback must restore every original control within floating-point parameter precision");
    processor.startAutoPreset();
    processor.setControl(drum::bodyGain, 1);
    processor.updateAutoPreset();
    require(!processor.autoPresetBusy() && !processor.canApplyAutoPreset(), "Manual changes must invalidate learning");
    processor.startAutoPreset();
    processor.cancelAutoPreset();
    require(!processor.autoPresetBusy() && !processor.canApplyAutoPreset(), "Cancellation must not leave an applicable preset");
    drum::DrumAutoCapture capture;
    juce::AudioBuffer<float> silence(2, 256);
    silence.clear();
    auto generation = capture.begin();
    capture.push(silence, 48000);
    require(!capture.ready(generation), "Capture must wait for the complete interval");
    capture.cancel();
    generation = capture.begin();
    for (int block = 0; block < 1500; ++block) capture.push(silence, 48000);
    require(capture.ready(generation) && capture.size() == 384000, "Restarted capture must discard the previous partial interval");
    require(capture.available(generation) == 384000 && capture.available(generation + 1) == 0,
        "Published length must follow the capture and ignore other generations");
    generation = capture.begin();
    capture.push(silence, 48000);
    capture.push(silence, 44100);
    require(capture.ready(generation) && capture.size() == 0, "Sample rate changes must reject a mixed-rate capture");
    processor.releaseResources();
}

void analyseWav(const juce::File& file, drum::DrumStyle style = drum::DrumStyle::natural)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    require(reader != nullptr, "WAV input must be readable");
    require(reader->numChannels >= 1 && reader->numChannels <= 2 && reader->sampleRate >= 8000
        && reader->sampleRate <= 192000 && reader->lengthInSamples <= reader->sampleRate * 3600,
        "WAV analysis supports mono/stereo, 8-192 kHz, at most one hour");
    drum::DrumAnalyser analyser;
    analyser.prepare(reader->sampleRate);
    juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), 32768);
    std::array<float, 32768> mono {};
    double dryEnergy = 0;
    float dryPeak = 0;
    for (juce::int64 offset = 0; offset < reader->lengthInSamples;)
    {
        const auto count = static_cast<int>(juce::jmin(juce::int64 { 32768 }, reader->lengthInSamples - offset));
        require(reader->read(&audio, 0, count, offset, true, true), "WAV source read must succeed");
        for (int sample = 0; sample < count; ++sample)
        {
            float value = 0;
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            {
                const auto candidate = audio.getSample(channel, sample);
                require(std::isfinite(candidate), "WAV samples must be finite");
                dryEnergy += static_cast<double>(candidate) * candidate;
                dryPeak = juce::jmax(dryPeak, std::abs(candidate));
                if (std::abs(candidate) > std::abs(value)) value = candidate;
            }
            mono[static_cast<size_t>(sample)] = value;
        }
        analyser.push(mono.data(), count);
        offset += count;
    }
    auto proposal = analyser.propose(drum::defaultSettings(), style);
    if (proposal.valid)
    {
        drum::DrumPresetPreview preview;
        for (int pass = 0; pass < 2; ++pass)
        {
            preview.prepare(reader->sampleRate, static_cast<int>(reader->numChannels), proposal);
            for (juce::int64 offset = 0; offset < reader->lengthInSamples;)
            {
                const auto count = static_cast<int>(juce::jmin(juce::int64 { 32768 }, reader->lengthInSamples - offset));
                require(reader->read(&audio, 0, count, offset, true, true), "Preview WAV read must succeed");
                preview.push(audio, count);
                offset += count;
            }
            if (pass == 0) preview.refineThreshold(proposal); else preview.finish(proposal);
        }
    }
    const auto sampleCount = static_cast<double>(reader->lengthInSamples) * reader->numChannels;
    std::cout << "WAV: " << file.getFileName() << " / " << reader->sampleRate << " Hz / " << reader->numChannels
        << " ch / " << reader->lengthInSamples / reader->sampleRate << " s / " << reader->bitsPerSample << " bits\n"
        << "Dry peak: " << juce::Decibels::gainToDecibels(dryPeak, -160.0f) << " dBFS / RMS: "
        << juce::Decibels::gainToDecibels(std::sqrt(dryEnergy / juce::jmax(1.0, sampleCount)), -160.0) << " dBFS\n"
        << "Proposal: " << (proposal.valid ? "VALID / " : "REFUSED / ") << proposal.summary << '\n';
    if (!proposal.valid) return;
    for (size_t index = 0; index < proposal.settings.size(); ++index)
        std::cout << drum::controlSpecs[index].id << " = " << proposal.settings[index] << '\n';
    drum::DrumEngine engine;
    engine.prepare(reader->sampleRate, 32768, static_cast<int>(reader->numChannels));
    double wetEnergy = 0;
    float wetPeak = 0, maximumExpansion = 0, maximumCompression = 0;
    for (juce::int64 offset = 0; offset < reader->lengthInSamples;)
    {
        const auto count = static_cast<int>(juce::jmin(juce::int64 { 32768 }, reader->lengthInSamples - offset));
        require(reader->read(&audio, 0, count, offset, true, true), "WAV render read must succeed");
        juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), audio.getNumChannels(), count);
        engine.process(block, proposal.settings);
        maximumExpansion = juce::jmax(maximumExpansion, engine.gateMeter.load());
        maximumCompression = juce::jmax(maximumCompression, engine.compressionMeter.load());
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            for (int sample = 0; sample < count; ++sample)
            {
                const auto value = block.getSample(channel, sample);
                require(std::isfinite(value), "Proposed processing must remain finite on the real WAV");
                wetPeak = juce::jmax(wetPeak, std::abs(value));
                wetEnergy += static_cast<double>(value) * value;
            }
        offset += count;
    }
    std::cout << "Wet peak: " << juce::Decibels::gainToDecibels(wetPeak, -160.0f) << " dBFS / RMS: "
        << juce::Decibels::gainToDecibels(std::sqrt(wetEnergy / juce::jmax(1.0, sampleCount)), -160.0)
        << " dBFS / max EXP: " << maximumExpansion << " dB / max COMP: " << maximumCompression << " dB\n"
        << "PASS: real WAV analysis and finite processing; source unchanged\n";
}

int main(int argumentCount, char** arguments)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI initialiser;
        if (argumentCount == 3 && juce::String(arguments[1]) == "--screenshots")
        {
            makeScreenshots(juce::File(juce::String::fromUTF8(arguments[2])));
            return 0;
        }
        if ((argumentCount == 3 || argumentCount == 4) && juce::String(arguments[1]) == "--analyse-wav")
        {
            const auto style = argumentCount == 4 ? juce::String(arguments[3]).toLowerCase() : juce::String("natural");
            require(style == "natural" || style == "rock" || style == "metal", "Style must be natural, rock or metal");
            analyseWav(juce::File(juce::String::fromUTF8(arguments[2])), style == "rock" ? drum::DrumStyle::rock
                : style == "metal" ? drum::DrumStyle::metal : drum::DrumStyle::natural);
            return 0;
        }
        juce::StringArray commandLine;
        for (int index = 1; index < argumentCount; ++index) commandLine.add(juce::String::fromUTF8(arguments[index]));
        if (const auto code = runReplayCommand(commandLine); code >= 0)
            return code;
        if (const auto code = runKitCommand(commandLine); code >= 0)
            return code;
        if (const auto code = runPairCommand(commandLine); code >= 0)
            return code;
        if (const auto code = runBusCommand(commandLine); code >= 0)
            return code;
        if (const auto code = runVersusCommand(commandLine); code >= 0)
            return code;
        if (const auto code = runSubSumCommand(commandLine); code >= 0)
            return code;
        testKit();
        testSnapshot();
        testAnalysisProperties();
        testReplay();
        testMeasures();
        testKnownProblems();
        testRoleReference();
        testPair();
        testAutoRadar();
        testPresets();
        testSubBuses();
        testVersus();
        testSignals();
        testKickResponse();
        testDrumBus();
        testKickArchive();
        testKickPresence();
        testMakeupScope();
        testAutoPreset();
        testAutoExpander();
        testTemporalAutoEQ();
        testAutoLiveCapture();
        testAutoWorkflow();
        {
            ZeroEffortDrumProcessor stateSource, stateTarget;
            stateSource.setControl(drum::presenceOn, 1);
            stateSource.setControl(drum::focusOn, 1);
            stateSource.setControl(drum::cleanupOn, 1);
            stateSource.setControl(drum::reinforceFrequency, 55);
            stateSource.setControl(drum::reinforceAmount, 42);
            stateSource.setControl(drum::reinforceInvert, 1);
            stateSource.setControl(drum::bodyQ, 2.3f);
            stateSource.setControl(drum::boxQ, 1.4f);
            stateSource.setControl(drum::attackQ, 1.8f);
            stateSource.setControl(drum::output, 6);
            stateSource.setControl(drum::multibandOn, 1);
            stateSource.setControl(drum::midThreshold, -32);
            stateSource.setControl(drum::crossoverLow, 170);
            stateSource.setControl(drum::instrument, 9);
            stateSource.setControl(drum::glueOn, 1);
            stateSource.setControl(drum::ambienceOn, 1);
            stateSource.setControl(drum::ambienceAmount, 3);
            stateSource.setAutoStyle(drum::DrumStyle::rock, 0.81f);
            juce::MemoryBlock state;
            stateSource.getStateInformation(state);
            stateTarget.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            require(stateTarget.getAutoStyle() == drum::DrumStyle::rock
                && std::abs(stateTarget.getAutoIntensity() - 0.81f) < 0.00001f,
                "Auto style and intensity must survive processor state persistence");
            require(stateTarget.getSettings()[drum::presenceOn] == 1
                && stateTarget.getSettings()[drum::reinforceFrequency] == 55
                && stateTarget.getSettings()[drum::reinforceAmount] == 42
                && stateTarget.getSettings()[drum::reinforceInvert] == 1,
                "All new presence settings must survive insert state persistence");
            require(std::abs(stateTarget.getSettings()[drum::bodyQ] - 2.3f) < 0.001f
                && std::abs(stateTarget.getSettings()[drum::boxQ] - 1.4f) < 0.001f
                && std::abs(stateTarget.getSettings()[drum::attackQ] - 1.8f) < 0.001f
                && std::abs(stateTarget.getSettings()[drum::output] - 6) < 0.001f,
                "Independent Q and positive output must survive insert persistence");
            require(stateTarget.getSettings()[drum::multibandOn] == 1
                && std::abs(stateTarget.getSettings()[drum::midThreshold] + 32) < 0.001f
                && std::abs(stateTarget.getSettings()[drum::crossoverLow] - 170) < 0.001f,
                "Insert state must persist multiband mode, crossover and per-band threshold");
            require(stateTarget.getSettings()[drum::instrument] == 9 && stateTarget.getSettings()[drum::glueOn] == 1
                && stateTarget.getSettings()[drum::ambienceAmount] == 3, "Insert state must preserve role and bus settings");
            auto oldState = stateSource.parameters.copyState();
            for (int index = oldState.getNumChildren(); --index >= 0;)
            {
                const auto id = oldState.getChild(index).getProperty("id").toString();
                for (size_t control = drum::focusOn; control < drum::controlSpecs.size(); ++control)
                    if (id == drum::controlSpecs[control].id) oldState.removeChild(index, nullptr);
            }
            juce::MemoryBlock oldData;
            juce::AudioProcessor::copyXmlToBinary(*oldState.createXml(), oldData);
            stateTarget.setStateInformation(oldData.getData(), static_cast<int>(oldData.getSize()));
            require(stateTarget.getSettings()[drum::focusOn] == 0
                && stateTarget.getSettings()[drum::presenceOn] == 0 && stateTarget.getSettings()[drum::cleanupOn] == 0,
                "Loading an old insert state into an active new instance must disable all new modules");
            require(stateTarget.getSettings()[drum::multibandOn] == 0,
                "Restoring an old insert state must never enable multiband in an already active instance");
            require(stateTarget.getSettings()[drum::instrument] == 0 && stateTarget.getSettings()[drum::glueOn] == 0
                && stateTarget.getSettings()[drum::ambienceOn] == 0, "Legacy insert state must reset role and bus modules");
        }
        {
            drum::DrumEngine engine;
            engine.prepare(48000, 256, 2);
            juce::AudioBuffer<float> audio(2, 256);
            auto settings = drum::defaultSettings();
            fill(audio);
            engine.process(audio, settings);
            require(audio.getSample(0, 255) == 0.25f, "Neutral Kick engine must be transparent");
            settings[drum::compressorOn] = 1;
            settings[drum::threshold] = -30;
            for (int block = 0; block < 100; ++block)
            {
                fill(audio);
                engine.process(audio, settings);
            }
            require(audio.getSample(0, 255) < 0.1f, "Compressor must reduce sustained signal");
            require(audio.getSample(0, 255) == audio.getSample(1, 255), "Kick dynamics must remain stereo linked");
            const auto heldReduction = engine.compressionDisplayMeter.load();
            settings[drum::compressorOn] = 0;
            for (int block = 0; block < 10; ++block)
            {
                audio.clear();
                engine.process(audio, settings);
            }
            require(engine.compressionMeter.load() == 0 && engine.compressionDisplayMeter.load() > heldReduction * 0.7f,
                "Display release must keep brief reductions visible without altering raw calibration measurements");
            for (int block = 0; block < 300; ++block)
            {
                audio.clear();
                engine.process(audio, settings);
            }
            require(engine.compressionDisplayMeter.load() < 0.1f, "Display meter must return toward zero after silence");
            engine.reset();
            require(engine.compressionDisplayMeter.load() == 0, "Seek reset must clear held gain reduction");
            settings[drum::bypass] = 1;
            for (int block = 0; block < 10; ++block)
            {
                fill(audio);
                engine.process(audio, settings);
            }
            require(std::abs(audio.getSample(0, 255) - 0.25f) < 0.00001f, "Global bypass must settle to dry unity");
            settings = drum::defaultSettings();
            settings[drum::output] = -12;
            engine.reset();
            fill(audio);
            engine.process(audio, settings);
            require(std::abs(audio.getSample(0, 0) - 0.25f * juce::Decibels::decibelsToGain(-12.0f)) < 0.00001f,
                "Seek reset must immediately initialise the current settings rather than resume an old ramp");
        }
        const auto* factory = createARAFactory();
        require(factory != nullptr, "ARA factory must exist");
        require(factory->informationURL != nullptr && juce::String(factory->informationURL).isNotEmpty(),
            "ARA SDK requires a non-empty information URL for host discovery");
        require(factory->plugInName != nullptr && juce::String(factory->plugInName) == "ZeroEffort Drum",
            "ARA plugin name must match ZeroEffort Drum");
        require(factory->manufacturerName != nullptr && juce::String(factory->manufacturerName).isNotEmpty(),
            "ARA manufacturer must be present");
        require(factory->version != nullptr && juce::String(factory->version) == "0.11.8",
            "ARA factory version must match the corrected prototype");
        require(juce::String(factory->factoryID) == "com.robin.zeroeffortdrum.factory.1",
            "ARA identity must identify ZeroEffort Drum separately from the prototype and gain plugin");
        require(juce::String(factory->documentArchiveID) == "com.robin.zeroeffortdrum.archive.1",
                "ARA archive must have its own identity");

        ZeroEffortDrumProcessor processor;
        require(processor.getName() == "ZeroEffort Drum", "Processor must expose the new product name");
        require(!processor.isBoundToARA(), "Unbound processor must support insert mode");
        require(processor.getTotalNumInputChannels() == 1 && processor.getTotalNumOutputChannels() == 1,
            "New instances must default to native mono");
        processor.prepareToPlay(48000.0, 64);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> nativeMono(1, 64);
        fill(nativeMono);
        processor.processBlock(nativeMono, midi);
        require(nativeMono.getSample(0, 0) == 0.25f && nativeMono.getSample(0, 63) == 0.25f,
            "Default mono processing must preserve the source level");
        processor.releaseResources();
        auto stereoLayout = processor.getBusesLayout();
        stereoLayout.inputBuses.set(0, juce::AudioChannelSet::stereo());
        stereoLayout.outputBuses.set(0, juce::AudioChannelSet::stereo());
        require(processor.setBusesLayout(stereoLayout), "Host must still be able to negotiate stereo");
        require(processor.getTotalNumOutputChannels() == 2, "Stereo negotiation must update the output bus");
        testEditor(processor, argumentCount == 2 ? juce::File(juce::String::fromUTF8(arguments[1]))
            : juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("DrumLive.png"));
        testAutoRadarFlow(argumentCount == 2 ? juce::File(juce::String::fromUTF8(arguments[1]))
            : juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("DrumLive.png"));
        require(processor.getSpectrumMailbox().subscribers.load() == 0, "Closing editor must release the spectrum subscription");
        processor.prepareToPlay(48000.0, 1024);
        juce::AudioBuffer<float> buffer(2, 1024);
        fill(buffer);
        processor.processBlock(buffer, midi);
        require(buffer.getSample(0, 0) == 0.25f, "Default insert must be transparent");

        auto* parameter = processor.parameters.getParameter("prototypeGainDb");
        parameter->setValueNotifyingHost(parameter->convertTo0to1(-6.0f));
        fill(buffer);
        processor.processBlock(buffer, midi);
        const auto expected = 0.25f * juce::Decibels::decibelsToGain(-6.0f);
        require(buffer.getSample(0, 0) < 0.25f && buffer.getSample(0, 0) > expected,
                "Gain transition must be smoothed");
        require(std::abs(buffer.getSample(0, 1023) - expected) < 0.00001f, "Gain must settle");
        for (int sample = 0; sample < 1024; ++sample)
            require(buffer.getSample(0, sample) == buffer.getSample(1, sample), "Stereo gain must stay linked");

        juce::MemoryBlock state;
        processor.setControl(drum::bodyGain, 4.5f);
        processor.setControl(drum::gateRange, 22);
        processor.getStateInformation(state);
        ZeroEffortDrumProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        restored.setStateInformation("invalid", 7);
        require(restored.parameters.getRawParameterValue("prototypeGainDb")->load() == -6.0f,
                "State must restore and reject invalid input");
        require(restored.getSettings()[drum::bodyGain] == 4.5f && restored.getSettings()[drum::gateRange] == 22,
            "New Kick controls must persist alongside the existing output gain");
        auto layout = restored.getBusesLayout();
        layout.inputBuses.set(0, juce::AudioChannelSet::mono());
        layout.outputBuses.set(0, juce::AudioChannelSet::mono());
        require(restored.setBusesLayout(layout), "Mono must be supported");
        restored.prepareToPlay(44100.0, 64);
        juce::AudioBuffer<float> mono(1, 64);
        fill(mono);
        restored.processBlock(mono, midi);
        require(std::abs(mono.getSample(0, 0) - expected) < 0.00001f, "Restored mono gain must match");
        juce::AudioBuffer<float> empty(1, 0);
        restored.processBlock(empty, midi);
        restored.releaseResources();
        processor.releaseResources();
        std::cout << "PASS: ARA factory, insert, gain, smoothing, stereo, state, invalid state, mono, empty\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

void testDrumBus()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        for (int channels : { 1, 2 })
        {
            std::array<double, 7> moduleEnergy {}, differenceEnergy {};
            for (size_t mode = 0; mode < moduleEnergy.size(); ++mode)
            {
                drum::DrumFinishDSP finish;
                finish.prepare(rate, 256);
                drum::FinishParameters settings;
                settings.crushOn = mode == 1 ? 1.0f : 0.0f;
                settings.crushAmount = 50;
                settings.colourOn = mode == 2 ? 1.0f : 0.0f;
                settings.colourDrive = 18;
                settings.colourAmount = 100;
                settings.clipOn = mode == 3 ? 1.0f : 0.0f;
                settings.clipCeiling = -12;
                settings.clipAmount = 100;
                settings.shapeOn = mode == 4 ? 1.0f : 0.0f;
                settings.shapeSustain = 6;
                settings.dynamicOn = mode == 5 ? 1.0f : 0.0f;
                settings.threshold = { -40, -40 };
                settings.range = { 3, 3 };
                settings.levelMatch = mode == 6 ? 1.0f : 0.0f;
                finish.configure(settings);
                for (int sample = 0; sample < static_cast<int>(rate * 2); ++sample)
                {
                    const auto time = static_cast<double>(sample) / rate;
                    const auto value = static_cast<float>(0.5 * std::sin(juce::MathConstants<double>::twoPi * 350 * time)
                        + 0.2 * std::sin(juce::MathConstants<double>::twoPi * 7000 * time));
                    const std::array<float, 2> original { value, -value };
                    auto signal = original;
                    if (mode == 6) for (auto& channel : signal) channel *= 0.5f;
                    finish.process(signal, original, channels);
                    require(std::isfinite(signal[0]) && std::abs(signal[0]) < 8,
                        "Finishing modules must give finite, bounded output at every rate");
                    if (channels == 2)
                        require(std::abs(signal[0] + signal[1]) < 0.00001f,
                            "Finishing must preserve opposite-polarity stereo channels without detector cancellation");
                    if (sample >= rate)
                    {
                        moduleEnergy[mode] += signal[0] * signal[0];
                        differenceEnergy[mode] += (signal[0] - value) * (signal[0] - value);
                    }
                }
                require(finish.reductions[0] <= 3.0001f && finish.reductions[1] <= 3.0001f,
                    "Dynamic EQ detectors must obey the requested reduction ceilings");
                if (mode == 1) require(finish.crushReduction > 1, "Crush must really compress its return");
                if (channels == 2) require(finish.correlation() < -0.99f,
                    "Opposite-polarity channels must give a negative correlation warning");
                if (mode == 5) require(finish.reductions[0] > 1 && finish.reductions[1] > 1,
                    "Both dynamic bands must engage on their respective tones");
                finish.reset();
                std::array<float, 2> silence {};
                for (int sample = 0; sample < 256; ++sample) finish.process(silence, silence, channels);
                require(silence[0] == 0 && silence[1] == 0, "Finishing reset must leave no stale tails or synthetic output");
            }
            require(differenceEnergy[0] == 0, "All finishing modules OFF must preserve the dry signal exactly");
            require(moduleEnergy[1] > moduleEnergy[0] * 1.05, "Crush return must add measurable density");
            require(differenceEnergy[2] > moduleEnergy[0] * 0.01 && differenceEnergy[3] > moduleEnergy[0] * 0.01,
                "Colour and soft clipping must affect the actual audio, not only parameter state");
            require(moduleEnergy[4] > moduleEnergy[0] * 1.5, "Sustain control must increase the body of sustained material");
            require(moduleEnergy[5] < moduleEnergy[0] * 0.8, "Dynamic EQ must attenuate loud material in its bands");
            require(std::abs(moduleEnergy[6] / moduleEnergy[0] - 1) < 0.02,
                "Level-matched A/B must converge after a six dB input/output difference");
        }
        {
            drum::DrumAnalyser measured;
            measured.prepare(rate);
            std::vector<float> body(static_cast<size_t>(rate * 3));
            for (size_t sample = 0; sample < body.size(); ++sample)
                body[sample] = static_cast<float>(0.4 * std::sin(juce::MathConstants<double>::twoPi * 150 * sample / rate));
            measured.push(body.data(), static_cast<int>(body.size()));
            for (const auto role : { drum::DrumInstrument::snare, drum::DrumInstrument::tom1 })
            {
                auto original = drum::defaultSettings();
                original[drum::instrument] = static_cast<float>(role);
                original[drum::autoMeasuredOn] = 1;
                const auto proposal = measured.propose(original, drum::DrumStyle::rock, 0.7f);
                require(proposal.valid && std::abs(proposal.settings[drum::bodyFrequency] - 150) < 12
                    && proposal.summary.contains("Measured body") && proposal.settings[drum::autoMeasuredOn] == 1,
                    "Measured Snare/Tom bodies must follow the source peak rather than a fixed frequency");
            }
            drum::DrumEngine monoMonitor;
            monoMonitor.prepare(rate, 256, 2);
            auto controls = drum::defaultSettings();
            controls[drum::monitorMonoOn] = 1;
            juce::AudioBuffer<float> opposite(2, 256);
            for (int sample = 0; sample < 256; ++sample)
            {
                opposite.setSample(0, sample, 0.25f);
                opposite.setSample(1, sample, -0.25f);
            }
            monoMonitor.process(opposite, controls);
            require(opposite.getMagnitude(0, 256) == 0 && monoMonitor.finishMeters[4].load() < -0.99f,
                "Mono monitoring must reveal cancellation while correlation measures the pre-monitor signal");
        }
        std::array<double, 3> insertTailEnergy {};
        const std::array<float, 3> doses { 0, 10, 100 };
        for (size_t doseIndex = 0; doseIndex < doses.size(); ++doseIndex)
        {
            ZeroEffortDrumProcessor insert;
            insert.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::master));
            insert.setControl(drum::ambienceOn, 1);
            insert.setControl(drum::ambienceAmount, doses[doseIndex]);
            insert.setControl(drum::ambienceSize, 100);
            insert.prepareToPlay(rate, 256);
            juce::AudioBuffer<float> insertAudio(2, 256);
            juce::MidiBuffer insertMidi;
            for (int block = 0; block < 32; ++block)
            {
                insertAudio.clear();
                insert.processBlock(insertAudio, insertMidi);
            }
            for (int block = 0; block < static_cast<int>(rate * 2 / 256); ++block)
            {
                insertAudio.clear();
                if (block == 0)
                    for (int channel = 0; channel < 2; ++channel)
                        insertAudio.setSample(channel, 0, 0.5f);
                insert.processBlock(insertAudio, insertMidi);
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < 256; ++sample)
                    {
                        const auto output = insertAudio.getSample(channel, sample);
                        require(std::isfinite(output), "Insert room output must stay finite");
                        if (block > 4)
                            insertTailEnergy[doseIndex] += output * output;
                    }
            }
            insert.releaseResources();
        }
        require(insertTailEnergy[0] < 1.0e-10, "Zero room dose must leave no insert reverb tail");
        require(insertTailEnergy[1] > 1.0e-6, "Room must reach the actual drum bus insert output");
        require(insertTailEnergy[2] > insertTailEnergy[1] * 90,
            "Full room dose must give a clearly stronger wet signal than the former ten percent ceiling");
        drum::DrumBusDSP bus;
        bus.prepare(rate, 256, 2);
        drum::DrumBusParameters settings;
        settings.compressorEnabled = true;
        settings.thresholdDb = -40;
        settings.rangeDb = 3;
        settings.attackMs = 0.1f;
        settings.sidechainHPHz = 0;
        bus.configure(settings);
        float maximum = 0;
        for (int sample = 0; sample < static_cast<int>(rate); ++sample)
        {
            std::array<float, 2> signal { 0.25f, -0.125f };
            const auto reduction = bus.process(signal, 2);
            maximum = juce::jmax(maximum, reduction);
            require(std::isfinite(signal[0]) && std::abs(signal[1] + signal[0] * 0.5f) < 0.000001f,
                "Glue must preserve stereo balance and phase with linked gain");
            require(reduction <= 3.0001f, "Glue reduction must obey its realtime ceiling");
        }
        require(maximum > 2.9f, "Glue must actually compress above threshold");
        settings.ratio = 1;
        bus.configure(settings);
        std::array<float, 2> signal { 0.25f, -0.125f };
        require(bus.process(signal, 2) == 0 && signal[0] == 0.25f, "Glue ratio one must give exact unity");
        settings.compressorEnabled = false;
        settings.ambienceEnabled = true;
        settings.ambiencePercent = 5;
        settings.predelayMs = 10;
        bus.configure(settings);
        bus.reset();
        double tailEnergy = 0;
        for (int sample = 0; sample < static_cast<int>(rate * 2); ++sample)
        {
            signal = { sample == 0 ? 0.5f : 0.0f, sample == 0 ? 0.5f : 0.0f };
            bus.process(signal, 2);
            require(std::isfinite(signal[0]) && std::isfinite(signal[1]), "Room impulse response must stay finite");
            if (sample == 0) require(signal[0] == 0.5f, "Additive ambience must preserve the direct attack");
            if (sample > rate * 0.02) tailEnergy += signal[0] * signal[0] + signal[1] * signal[1];
        }
        require(tailEnergy > 0.0000001 && tailEnergy < 0.02, "Short room must produce a restrained nonzero tail");
        settings.character = settings.density = 100;
        bus.configure(settings);
        bus.reset();
        double enhancedTailEnergy = 0;
        for (int sample = 0; sample < static_cast<int>(rate * 2); ++sample)
        {
            signal = { sample == 0 ? 0.5f : 0.0f, sample == 0 ? 0.5f : 0.0f };
            bus.process(signal, 2);
            require(std::isfinite(signal[0]) && std::isfinite(signal[1]), "Enhanced room must stay finite");
            if (sample == 0) require(signal[0] == 0.5f, "Room character/density must preserve the direct attack");
            if (sample > rate * 0.02) enhancedTailEnergy += signal[0] * signal[0] + signal[1] * signal[1];
        }
        require(enhancedTailEnergy > tailEnergy * 1.1, "Room character and compressed return must produce a measurable change");
        bus.reset();
        signal.fill(0);
        bus.process(signal, 2);
        require(signal[0] == 0 && signal[1] == 0, "Room reset must clear all tails");
        drum::DrumEngine engine;
        engine.prepare(rate, 256, 2);
        auto controls = drum::defaultSettings();
        controls[drum::glueOn] = controls[drum::ambienceOn] = controls[drum::bypass] = 1;
        controls[drum::glueThreshold] = -50;
        juce::AudioBuffer<float> audio(2, 256);
        fill(audio);
        engine.process(audio, controls);
        require(audio.getSample(0, 255) == 0.25f, "Global bypass must remove Glue and room completely");
        std::vector<float> performance(static_cast<size_t>(rate * 3));
        for (size_t sample = 0; sample < performance.size(); ++sample)
        {
            const auto time = sample / rate;
            performance[sample] = static_cast<float>(std::exp(-std::fmod(time, 0.25) * 18)
                * (0.3 * std::sin(juce::MathConstants<double>::twoPi * 65 * time)
                    + 0.15 * std::sin(juce::MathConstants<double>::twoPi * 180 * time)
                    + 0.06 * std::sin(juce::MathConstants<double>::twoPi * 4500 * time)));
        }
        auto analyser = std::make_unique<drum::DrumAnalyser>();
        analyser->prepare(rate);
        analyser->push(performance.data(), static_cast<int>(performance.size()));
        auto preview = std::make_unique<drum::DrumPresetPreview>();
        for (int element = 1; element < 10; ++element)
        for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
        {
            auto original = drum::defaultSettings();
            original[drum::instrument] = static_cast<float>(element);
            auto proposal = analyser->propose(original, style);
            require(proposal.valid, "Every non-Kick profile must analyse a finite percussion performance");
            if (element == 9)
                require(proposal.settings[drum::glueOn] == 1 && proposal.settings[drum::compressorOn] == 0
                    && proposal.settings[drum::crushAmount] <= 18
                    && (proposal.settings[drum::ambienceOn] == 0 || proposal.settings[drum::ambienceAmount] <= 4),
                    "Master Auto must keep Glue as its main compressor and use restrained finishing returns");
            juce::AudioBuffer<float> chunk(1, 256);
            for (int pass = 0; pass < 2; ++pass)
            {
                preview->prepare(rate, 1, proposal);
                for (size_t offset = 0; offset < performance.size(); offset += 256)
                {
                    const auto count = static_cast<int>(juce::jmin(size_t { 256 }, performance.size() - offset));
                    chunk.copyFrom(0, 0, performance.data() + offset, count);
                    preview->push(chunk, count);
                }
                if (pass == 0) preview->refineThreshold(proposal); else preview->finish(proposal);
            }
            require(proposal.valid, "Per-instrument calibrated Bilan must remain valid");
            if (element == 9) require(proposal.summary.contains("BUS GLUE") && proposal.summary.contains("sum only"),
                "Master Bilan must show calibrated Glue and disclose no individual track measurements");
            engine.prepare(rate, 256, 1);
            double dryEnergy = 0, wetEnergy = 0;
            float maximumPeak = 0, maximumReduction = 0;
            for (size_t offset = 0; offset < performance.size(); offset += 256)
            {
                const auto count = static_cast<int>(juce::jmin(size_t { 256 }, performance.size() - offset));
                chunk.copyFrom(0, 0, performance.data() + offset, count);
                juce::AudioBuffer<float> block(chunk.getArrayOfWritePointers(), 1, count);
                engine.process(block, proposal.settings);
                maximumPeak = juce::jmax(maximumPeak, block.getMagnitude(0, count));
                maximumReduction = juce::jmax(maximumReduction, engine.compressionMeter.load());
                for (int sample = 0; sample < count; ++sample)
                {
                    const auto wet = block.getSample(0, sample);
                    const auto dry = performance[offset + static_cast<size_t>(sample)];
                    require(std::isfinite(wet), "Every calibrated role must render finite samples");
                    dryEnergy += dry * dry;
                    wetEnergy += wet * wet;
                }
            }
            require(maximumPeak <= juce::Decibels::decibelsToGain(-0.95f), "Every role must retain verified sample headroom");
            if (element == 9)
                require(maximumReduction > 0.1f && maximumReduction <= proposal.maximumReduction + 0.001f
                    && std::abs(10 * std::log10(wetEnergy / dryEnergy)) < 0.08,
                    "Master Auto must engage real Glue, respect GR max and restore RMS");
        }
    }
}
