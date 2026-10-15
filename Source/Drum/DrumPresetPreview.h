#pragma once
#include "DrumAutoPreset.h"

namespace drum
{
class DrumPresetPreview
{
public:
    float bandActiveReduction(size_t band) const
    {
        return percentileReduction((*bandActivity)[band], bandPeaks[band]);
    }

    void prepare(double sampleRate, int channels, const DrumProposal& proposal)
    {
        settings = proposal.settings;
        engine.prepare(sampleRate, 256, channels);
        scratch.setSize(channels, 256);
        dryEnergy = wetEnergy = 0;
        wetPeak = maximumReduction = 0;
        samples = 0;
        blocks = 0;
        activity.fill(0);
        glueEnvelopes->fill(0);
        for (auto& histogram : *bandActivity) histogram.fill(0);
        for (auto& histogram : *bandEnvelopes) histogram.fill(0);
        bandPeaks.fill(0);
        bandMaximums.fill(0);
        inputPeak = 0;
        expansionSamples = audibleSamples = 0;
        maximumExpansion = 0;
        maximumCleanup = maximumBody = 0;
        renderedHits = 0;
    }

    void push(const juce::AudioBuffer<float>& audio, int count)
    {
        for (int offset = 0; offset < count;)
        {
            const auto length = juce::jmin(256, count - offset);
            float blockPeak = 0;
            for (int channel = 0; channel < scratch.getNumChannels(); ++channel)
            {
                scratch.copyFrom(channel, 0, audio, channel, offset, length);
                for (int sample = 0; sample < length; ++sample)
                {
                    const auto value = scratch.getSample(channel, sample);
                    dryEnergy += static_cast<double>(value) * value;
                    blockPeak = juce::jmax(blockPeak, std::abs(value));
                }
            }
            juce::AudioBuffer<float> block(scratch.getArrayOfWritePointers(), scratch.getNumChannels(), length);
            engine.process(block, settings);
            for (size_t band = 0; band < 3; ++band)
            {
                const auto peak = engine.bandPeakMeters[band].load();
                const auto reduction = engine.bandReductionMeters[band].load();
                bandPeaks[band] = juce::jmax(bandPeaks[band], peak);
                bandMaximums[band] = juce::jmax(bandMaximums[band], reduction);
                const auto level = juce::jlimit(0, 100, static_cast<int>(juce::Decibels::gainToDecibels(peak, -100.0f) + 100));
                const auto gr = juce::jlimit(0, 240, static_cast<int>(reduction * 10));
                ++(*bandActivity)[band][static_cast<size_t>(level * 241 + gr)];
                const auto envelope = juce::jlimit(0, 240, static_cast<int>((engine.bandEnvelopeMeters[band].load() + 120) * 2));
                ++(*bandEnvelopes)[band][static_cast<size_t>(level * 241 + envelope)];
            }
            const auto reduction = engine.compressionMeter.load();
            maximumReduction = juce::jmax(maximumReduction, reduction);
            const auto expansion = engine.gateMeter.load();
            maximumExpansion = juce::jmax(maximumExpansion, expansion);
            maximumCleanup = juce::jmax(maximumCleanup, engine.cleanupMeter.load());
            maximumBody = juce::jmax(maximumBody, engine.reinforcementMeter.load());
            renderedHits = engine.detectedHits.load();
            if (blockPeak > 0.0001f)
            {
                audibleSamples += length;
                if (expansion > 1) expansionSamples += length;
            }
            inputPeak = juce::jmax(inputPeak, blockPeak);
            const auto levelBin = juce::jlimit(0, 100, static_cast<int>(juce::Decibels::gainToDecibels(blockPeak, -100.0f) + 100));
            const auto reductionBin = juce::jlimit(0, 240, static_cast<int>(reduction * 10));
            ++activity[static_cast<size_t>(levelBin * 241 + reductionBin)];
            const auto glueLevel = juce::jlimit(0, 240, static_cast<int>((engine.glueEnvelopeMeter.load() + 120) * 2));
            ++(*glueEnvelopes)[static_cast<size_t>(levelBin * 241 + glueLevel)];
            ++blocks;
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                for (int sample = 0; sample < length; ++sample)
                {
                    const auto value = block.getSample(channel, sample);
                    wetEnergy += static_cast<double>(value) * value;
                    wetPeak = juce::jmax(wetPeak, std::abs(value));
                    samples += 1;
                }
            offset += length;
        }
    }

    void refineThreshold(DrumProposal& proposal) const
    {
        if (proposal.settings[glueOn] > 0.5f && blocks > 0)
        {
            const auto level = percentileReduction(*glueEnvelopes, inputPeak) * 5 - 120;
            const auto slope = 1 - 1 / proposal.settings[glueRatio];
            const auto target = proposal.targetReduction;
            const auto knee = proposal.settings[glueKnee];
            const auto above = target < slope * knee * 0.5f
                ? std::sqrt(2 * knee * target / slope) - knee * 0.5f : target / slope;
            proposal.settings[glueThreshold] = juce::jlimit(-60.0f, 0.0f, level - above);
            return;
        }
        if (proposal.settings[compressorOn] < 0.5f || blocks == 0) return;
        if (proposal.settings[multibandOn] > 0.5f)
        {
            for (size_t band = 0; band < 3; ++band)
            {
                const auto base = static_cast<size_t>(lowThreshold) + band * 7;
                if (bandPeaks[band] < 0.00001f)
                {
                    proposal.settings[base + 1] = 1;
                    continue;
                }
                const auto slope = 1 - 1 / proposal.settings[base + 1];
                if (slope <= 0) continue;
                const auto envelope = percentileReduction((*bandEnvelopes)[band], bandPeaks[band]) * 5 - 120;
                if (envelope - proposal.bandTargets[band] / slope < controlSpecs[base].minimum)
                {
                    proposal.settings[base + 1] = 1;
                    proposal.bandTargets[band] = 0;
                    continue;
                }
                proposal.settings[base] = juce::jlimit(-80.0f, 0.0f, envelope - proposal.bandTargets[band] / slope);
            }
            return;
        }
        const auto slope = 1 - 1 / proposal.settings[ratio];
        const auto representative = activeReduction();
        const auto correction = juce::jlimit(-12.0f, 24.0f, juce::jmax(
            (representative - proposal.targetReduction) / slope,
            (maximumReduction - proposal.maximumReduction) / slope));
        proposal.settings[threshold] = juce::jlimit(-60.0f, 0.0f, proposal.settings[threshold] + correction);
    }

    void finish(DrumProposal& proposal) const
    {
        if (samples == 0 || dryEnergy <= 0 || wetEnergy <= 0 || !std::isfinite(wetEnergy))
        {
            proposal.valid = false;
            proposal.summary = "Verification du rendu impossible";
            return;
        }
        const auto compensation = static_cast<float>(10 * std::log10(dryEnergy / wetEnergy));
        const auto peakHeadroom = -1.0f - juce::Decibels::gainToDecibels(wetPeak, -160.0f);
        const auto adjustment = juce::jmin(compensation, peakHeadroom);
        const bool finishing = settings[levelMatchOn] + settings[crushOn] + settings[colourOn]
            + settings[clipOn] + settings[shapeOn] + settings[dynamicOn] > 0.5f;
        proposal.settings[output] = juce::jlimit(-60.0f, finishing ? 12.0f : 0.0f, settings[output] + adjustment);
        auto actualChange = proposal.settings[output] - settings[output];
        const auto compensationControl = settings[glueOn] > 0.5f ? glueMakeup : makeup;
        const auto compensationMix = settings[glueOn] > 0.5f ? glueMix : mix;
        if (!finishing && (settings[compressorOn] > 0.5f || settings[glueOn] > 0.5f) && settings[compensationMix] >= 100.0f && adjustment > actualChange)
        {
            proposal.settings[compensationControl] = juce::jlimit(controlSpecs[compensationControl].minimum, controlSpecs[compensationControl].maximum,
                settings[compensationControl] + adjustment - actualChange);
            actualChange += proposal.settings[compensationControl] - settings[compensationControl];
        }
        proposal.summary += " / EXP max " + juce::String(maximumExpansion, 1)
            + " dB / EXP >1 dB sur " + juce::String(audibleSamples > 0
                ? 100.0 * static_cast<double>(expansionSamples) / static_cast<double>(audibleSamples) : 0, 1)
            + "% des blocs audibles / COMP actif P75 " + juce::String(activeReduction(), 1)
            + " dB / rendu COMP max " + juce::String(maximumReduction, 1)
            + " dB / ecart RMS " + juce::String(actualChange - compensation, 1)
            + " dB / MAKEUP " + juce::String(proposal.settings[compensationControl], 1)
            + " dB / OUT " + juce::String(proposal.settings[output], 1) + " dB";
        if (settings[multibandOn] > 0.5f)
        {
            proposal.summary += "\n\nCOMPRESSION MULTIBANDE / LR24\nCoupures "
                + juce::String(settings[crossoverLow], 1) + " Hz / " + juce::String(settings[crossoverHigh], 0) + " Hz";
            const std::array<const char*, 3> names { "Grave", "Medium", "Aigu" };
            for (size_t band = 0; band < 3; ++band)
            {
                const auto base = static_cast<size_t>(lowThreshold) + band * 7;
                proposal.summary += "\n" + juce::String(names[band]) + " / seuil " + juce::String(settings[base], 1)
                    + " dB / ratio " + juce::String(settings[base + 1], 2)
                    + " / attack " + juce::String(settings[base + 2], 1) + " ms / release " + juce::String(settings[base + 3], 0)
                    + " ms / cible P75 " + juce::String(proposal.bandTargets[band], 1)
                    + " dB / P75 mesure " + juce::String(percentileReduction((*bandActivity)[band], bandPeaks[band]), 1)
                    + " dB / max " + juce::String(bandMaximums[band], 1)
                    + " dB / plafond " + juce::String(proposal.bandCeilings[band], 1) + " dB";
                if (settings[base + 1] == 1) proposal.summary += " / non comprimee : energie insuffisante";
            }
        }
        if (settings[glueOn] > 0.5f)
            proposal.summary += "\n\nBUS GLUE / seuil " + juce::String(settings[glueThreshold], 1)
                + " dB / ratio " + juce::String(settings[glueRatio], 1)
                + " / attaque " + juce::String(settings[glueAttack], 1) + " ms / release " + juce::String(settings[glueRelease], 0)
                + " ms / sidechain HP " + juce::String(settings[glueSidechain], 0)
                + " Hz / knee " + juce::String(settings[glueKnee], 1)
                + " dB / cible P75 blocs actifs " + juce::String(proposal.targetReduction, 1)
                + " dB / P75 GR blocs actifs " + juce::String(activeReduction(), 1)
                + " dB / max " + juce::String(maximumReduction, 1) + " dB / plafond moteur "
                + juce::String(settings[glueRange], 1) + " dB / ambiance "
                + (settings[ambienceOn] > 0.5f ? juce::String(settings[ambienceAmount], 1) + "%" : "OFF");
        proposal.summary += "\n\nVERIFICATION REELLE\n";
        if (instrumentFromValue(settings[instrument]) == DrumInstrument::kick)
            proposal.summary += "Declenchements Focus : " + juce::String(renderedHits)
                + " / nettoyage aigu max " + juce::String(maximumCleanup, 1)
                + " dB / renfort crete avant EQ-COMP "
                + juce::String(juce::Decibels::gainToDecibels(maximumBody, -160.0f), 1) + " dBFS\n";
        else proposal.summary += "Element " + juce::String(instrumentName(instrumentFromValue(settings[instrument])))
            + " / Focus et renfort synthetique Kick desactives"
            + " / Bleed Clean max " + juce::String(maximumCleanup, 1) + " dB"
            + (settings[cleanupOn] > 0.5f ? " / detecteur adapte au corps\n" : " / OFF\n");
        proposal.summary += "Pas de separation de sources, limiteur ni garantie de phase avec les autres micros."
            "\n\nREGLAGES FINAUX\n";
        for (size_t index = 0; index < proposal.settings.size(); ++index)
            proposal.summary += juce::String(controlSpecs[index].id) + " = "
                + juce::String(proposal.settings[index], 2) + " " + controlSpecs[index].unit + "\n";
    }

private:
    float activeReduction() const
    {
        return percentileReduction(activity, inputPeak);
    }

    static float percentileReduction(const std::array<int, 101 * 241>& histogram, float peak)
    {
        const auto firstLevel = juce::jlimit(0, 100,
            static_cast<int>(juce::Decibels::gainToDecibels(peak, -100.0f) + 100 - 18));
        std::array<int, 241> distribution {};
        int total = 0;
        for (int level = firstLevel; level <= 100; ++level)
            for (size_t reduction = 0; reduction < distribution.size(); ++reduction)
            {
                const auto count = histogram[static_cast<size_t>(level * 241) + reduction];
                distribution[reduction] += count;
                total += count;
            }
        if (total == 0) return 0;
        int accumulated = 0;
        for (size_t reduction = 0; reduction < distribution.size(); ++reduction)
        {
            accumulated += distribution[reduction];
            if (accumulated >= static_cast<int>(std::ceil(total * 0.75))) return static_cast<float>(reduction) / 10;
        }
        return 0;
    }

    DrumEngine engine;
    Settings settings {};
    juce::AudioBuffer<float> scratch;
    int blocks = 0;
    juce::int64 samples = 0;
    juce::int64 expansionSamples = 0, audibleSamples = 0;
    double dryEnergy = 0, wetEnergy = 0;
    float wetPeak = 0, maximumReduction = 0;
    float inputPeak = 0;
    float maximumExpansion = 0;
    float maximumCleanup = 0, maximumBody = 0;
    int renderedHits = 0;
    std::array<int, 101 * 241> activity {};
    std::unique_ptr<std::array<int, 101 * 241>> glueEnvelopes = std::make_unique<std::array<int, 101 * 241>>();
    std::unique_ptr<std::array<std::array<int, 101 * 241>, 3>> bandActivity
        = std::make_unique<std::array<std::array<int, 101 * 241>, 3>>();
    std::unique_ptr<std::array<std::array<int, 101 * 241>, 3>> bandEnvelopes
        = std::make_unique<std::array<std::array<int, 101 * 241>, 3>>();
    std::array<float, 3> bandPeaks {}, bandMaximums {};
};
}