#pragma once
#include "DrumAutoPreset.h"

#ifndef ZED_ANALYSIS_EXPORT
#define ZED_ANALYSIS_EXPORT 0
#endif

namespace drum
{
inline constexpr int snapshotSchema = 1;

// Everything propose() needs besides the measurements.
struct SnapshotContext
{
    Settings original = defaultSettings();
    DrumStyle style = DrumStyle::natural;
    float intensity = 1;
    juce::String source { "unknown" }, licence { "private-dev" };
};

struct RestoredSnapshot
{
    SnapshotContext context;
    DrumProposal recorded;
};

// JSON snapshot of one analysis: { meta, input, analysis, measures, proposal }.
// Spectra are stored as little-endian doubles in base64 so that a restored analysis replays bit for bit.
struct DrumAnalysisSnapshot
{
    static const char* styleId(DrumStyle style) noexcept
    {
        return style == DrumStyle::rock ? "rock" : style == DrumStyle::metal ? "metal" : "natural";
    }

    static bool styleFromId(const juce::String& text, DrumStyle& style) noexcept
    {
        if (text == "natural") style = DrumStyle::natural;
        else if (text == "rock") style = DrumStyle::rock;
        else if (text == "metal") style = DrumStyle::metal;
        else return false;
        return true;
    }

    static juce::var settingsToVar(const Settings& settings)
    {
        auto* object = new juce::DynamicObject();
        for (size_t index = 0; index < settings.size(); ++index)
            object->setProperty(controlSpecs[index].id, static_cast<double>(settings[index]));
        return juce::var(object);
    }

    // Unknown identifiers are ignored and missing ones keep their default, so older and newer files load.
    static juce::Result settingsFromVar(const juce::var& value, Settings& settings)
    {
        auto* object = value.getDynamicObject();
        if (object == nullptr) return juce::Result::fail("settings block missing");
        settings = defaultSettings();
        for (size_t index = 0; index < settings.size(); ++index)
        {
            const auto& entry = object->getProperty(controlSpecs[index].id);
            if (entry.isVoid()) continue;
            if (!isNumber(entry) || !std::isfinite(static_cast<double>(entry)))
                return juce::Result::fail(juce::String("invalid setting ") + controlSpecs[index].id);
            settings[index] = juce::jlimit(controlSpecs[index].minimum, controlSpecs[index].maximum,
                static_cast<float>(static_cast<double>(entry)));
        }
        return juce::Result::ok();
    }

    static juce::var numberOrNull(double value)
    {
        return std::isfinite(value) ? juce::var(value) : juce::var();
    }

    static juce::var measuresToVar(const DrumAnalysis& analysis, DrumInstrument role = DrumInstrument::kick)
    {
        auto* object = new juce::DynamicObject();
        const auto samples = static_cast<double>(analysis.samples);
        object->setProperty("lvl_peak_dbfs", analysis.peakDb());
        object->setProperty("lvl_rms_global_dbfs", analysis.rmsDb());
        object->setProperty("lvl_rms_active_dbfs", numberOrNull(analysis.activeRmsDb()));
        object->setProperty("lvl_noise_floor_dbfs", numberOrNull(analysis.noiseFloorDb()));
        object->setProperty("lvl_active_threshold_dbfs", numberOrNull(analysis.windows > 0 ? analysis.activeThresholdDb() : DrumAnalysis::unavailable()));
        object->setProperty("lvl_active_windows", analysis.activeWindowCount());
        object->setProperty("lvl_clipped_ratio", analysis.samples > 0 ? static_cast<double>(analysis.clipped) / samples : 0.0);
        for (const auto fraction : { 0.10f, 0.25f, 0.50f, 0.75f, 0.90f, 0.95f, 0.99f })
            object->setProperty("lvl_env_p" + juce::String(juce::roundToInt(fraction * 100)), analysis.percentile(fraction));
        object->setProperty("dyn_crest_global_db", analysis.crestDb());
        object->setProperty("dyn_crest_active_db", numberOrNull(analysis.activeCrestDb()));
        object->setProperty("hit_hits", analysis.hits);
        object->setProperty("hit_hits_per_s", numberOrNull(analysis.hitsPerSecond()));
        object->setProperty("hit_decay_t20_ms", numberOrNull(analysis.decayT20Ms()));
        object->setProperty("hit_decay_t60_ms", numberOrNull(analysis.decayT60Ms()));
        object->setProperty("hit_decay_observed", analysis.decayObservedHits());
        object->setProperty("hit_decay_censored", analysis.decayCensoredHits());
        object->setProperty("duration_s", analysis.rate > 0 ? samples / analysis.rate : 0.0);
        object->setProperty("ph_onset_frames", analysis.onsetFrames);
        object->setProperty("ph_tail_frames", analysis.tailFrames);
        object->setProperty("ph_bleed_frames", analysis.bleedFrames);
        object->setProperty("ph_attack_body_gap_db", numberOrNull(analysis.attackBodyGapDb(role)));
        for (int band = 0; band < thirdOctaveCount; ++band)
            object->setProperty("sp_third_oct_" + thirdOctaveLabel(band), numberOrNull(analysis.thirdOctaveDb(band)));
        for (const auto& band : namedBands)
            object->setProperty(juce::String("band_") + band.id, numberOrNull(analysis.bandRelativeDb(band.lower, band.upper)));
        return juce::var(object);
    }

    // Nominal centre frequencies, as written in the column names.
    static juce::String thirdOctaveLabel(int index)
    {
        static const char* const labels[thirdOctaveCount] { "20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160",
            "200", "250", "315", "400", "500", "630", "800", "1000", "1250", "1600", "2000", "2500", "3150", "4000", "5000",
            "6300", "8000", "10000", "12500", "16000", "20000" };
        return labels[juce::jlimit(0, thirdOctaveCount - 1, index)];
    }

    static juce::var analysisToVar(const DrumAnalysis& analysis)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("rate", analysis.rate);
        object->setProperty("fftSize", analysis.fftSize);
        object->setProperty("samples", analysis.samples);
        object->setProperty("clipped", analysis.clipped);
        object->setProperty("energy", analysis.energy);
        object->setProperty("peak", static_cast<double>(analysis.peak));
        object->setProperty("invalid", analysis.invalid);
        object->setProperty("windows", analysis.windows);
        object->setProperty("hits", analysis.hits);
        object->setProperty("onsetFrames", analysis.onsetFrames);
        object->setProperty("tailFrames", analysis.tailFrames);
        object->setProperty("bleedFrames", analysis.bleedFrames);
        object->setProperty("histogram", counts(analysis.histogram));
        juce::Array<juce::var> envelopes;
        for (const auto& values : analysis.envelopeHistograms) envelopes.add(counts(values));
        object->setProperty("envelopeHistograms", envelopes);
        const auto bins = static_cast<size_t>(analysis.fftSize / 2);
        object->setProperty("spectrum", encode(analysis.spectrum, bins));
        object->setProperty("onsetSpectrum", encode(analysis.onsetSpectrum, bins));
        object->setProperty("tailSpectrum", encode(analysis.tailSpectrum, bins));
        object->setProperty("bleedSpectrum", encode(analysis.bleedSpectrum, bins));
        object->setProperty("windowEnergyByPeak", encode(analysis.windowEnergyByPeak, analysis.windowEnergyByPeak.size()));
        object->setProperty("decayHistogram", counts(analysis.decayHistogram));
        object->setProperty("decayCensored", analysis.decayCensored);
        return juce::var(object);
    }

    static juce::Result restoreAnalysis(DrumAnalysis& analysis, const juce::var& value)
    {
        auto* object = value.getDynamicObject();
        if (object == nullptr) return juce::Result::fail("analysis block missing");
        double rate = 0, energy = 0, peak = 0;
        juce::int64 samples = 0, clipped = 0, windows = 0, hits = 0, onsetFrames = 0, tailFrames = 0, bleedFrames = 0;
        if (!readNumber(*object, "rate", rate) || !std::isfinite(rate) || rate < 8000 || rate > 192000)
            return juce::Result::fail("invalid rate");
        if (!readNumber(*object, "energy", energy) || !std::isfinite(energy) || energy < 0
            || !readNumber(*object, "peak", peak) || !std::isfinite(peak) || peak < 0)
            return juce::Result::fail("invalid energy or peak");
        if (!readInteger(*object, "samples", samples) || !readInteger(*object, "clipped", clipped)
            || !readInteger(*object, "windows", windows) || !readInteger(*object, "hits", hits)
            || !readInteger(*object, "onsetFrames", onsetFrames) || !readInteger(*object, "tailFrames", tailFrames)
            || !readInteger(*object, "bleedFrames", bleedFrames))
            return juce::Result::fail("missing counter");
        if (samples < 0 || clipped < 0 || clipped > samples || windows < 0 || hits < 0
            || onsetFrames < 0 || tailFrames < 0 || bleedFrames < 0
            || windows > std::numeric_limits<int>::max() || hits > std::numeric_limits<int>::max()
            || onsetFrames > std::numeric_limits<int>::max() || tailFrames > std::numeric_limits<int>::max()
            || bleedFrames > std::numeric_limits<int>::max())
            return juce::Result::fail("counter out of range");
        const auto& invalid = object->getProperty("invalid");
        if (!invalid.isBool()) return juce::Result::fail("invalid flag missing");

        std::array<int, 101> histogram {};
        std::array<std::array<int, 101>, 6> envelopes {};
        if (!readCounts(object->getProperty("histogram"), histogram))
            return juce::Result::fail("invalid histogram");
        const auto* envelopeArray = object->getProperty("envelopeHistograms").getArray();
        if (envelopeArray == nullptr || envelopeArray->size() != static_cast<int>(envelopes.size()))
            return juce::Result::fail("invalid envelope histograms");
        for (size_t index = 0; index < envelopes.size(); ++index)
            if (!readCounts((*envelopeArray)[static_cast<int>(index)], envelopes[index]))
                return juce::Result::fail("invalid envelope histogram");

        std::array<int, 101> decayHistogram {};
        juce::int64 decayCensored = 0;
        if (!readCounts(object->getProperty("decayHistogram"), decayHistogram))
            return juce::Result::fail("invalid decay histogram");
        if (!readInteger(*object, "decayCensored", decayCensored) || decayCensored < 0
            || decayCensored > std::numeric_limits<int>::max())
            return juce::Result::fail("invalid decay counter");

        analysis.prepare(rate);
        const auto bins = static_cast<size_t>(analysis.fftSize / 2);
        if (!decode(object->getProperty("spectrum").toString(), analysis.spectrum, bins)
            || !decode(object->getProperty("onsetSpectrum").toString(), analysis.onsetSpectrum, bins)
            || !decode(object->getProperty("tailSpectrum").toString(), analysis.tailSpectrum, bins)
            || !decode(object->getProperty("bleedSpectrum").toString(), analysis.bleedSpectrum, bins)
            || !decode(object->getProperty("windowEnergyByPeak").toString(), analysis.windowEnergyByPeak, analysis.windowEnergyByPeak.size()))
        {
            analysis.prepare(rate);
            return juce::Result::fail("invalid spectrum");
        }
        analysis.decayHistogram = decayHistogram;
        analysis.decayCensored = static_cast<int>(decayCensored);
        analysis.samples = samples;
        analysis.clipped = clipped;
        analysis.energy = energy;
        analysis.peak = static_cast<float>(peak);
        analysis.invalid = static_cast<bool>(invalid);
        analysis.windows = static_cast<int>(windows);
        analysis.hits = static_cast<int>(hits);
        analysis.onsetFrames = static_cast<int>(onsetFrames);
        analysis.tailFrames = static_cast<int>(tailFrames);
        analysis.bleedFrames = static_cast<int>(bleedFrames);
        analysis.histogram = histogram;
        analysis.envelopeHistograms = envelopes;
        return juce::Result::ok();
    }

    static juce::var toSnapshot(const DrumAnalysis& analysis, const SnapshotContext& context, const DrumProposal& proposal)
    {
        auto* meta = new juce::DynamicObject();
        meta->setProperty("schema", snapshotSchema);
        meta->setProperty("analysisVersion", analysisVersion);
        meta->setProperty("profileVersion", profileVersion);
        meta->setProperty("role", instrumentName(instrumentFromValue(context.original[instrument])));
        meta->setProperty("style", styleId(context.style));
        meta->setProperty("intensity", static_cast<double>(context.intensity));
        meta->setProperty("source", context.source);
        meta->setProperty("licence", context.licence);

        auto* input = new juce::DynamicObject();
        input->setProperty("settings", settingsToVar(context.original));

        auto* recorded = new juce::DynamicObject();
        recorded->setProperty("valid", proposal.valid);
        recorded->setProperty("summary", proposal.summary);
        recorded->setProperty("hits", proposal.hits);
        recorded->setProperty("targetReduction", static_cast<double>(proposal.targetReduction));
        recorded->setProperty("maximumReduction", static_cast<double>(proposal.maximumReduction));
        recorded->setProperty("bandTargets", floats(proposal.bandTargets));
        recorded->setProperty("bandCeilings", floats(proposal.bandCeilings));
        recorded->setProperty("settings", settingsToVar(proposal.settings));

        auto* root = new juce::DynamicObject();
        root->setProperty("meta", juce::var(meta));
        root->setProperty("input", juce::var(input));
        root->setProperty("analysis", analysisToVar(analysis));
        root->setProperty("measures", measuresToVar(analysis, instrumentFromValue(context.original[instrument])));
        root->setProperty("proposal", juce::var(recorded));
        return juce::var(root);
    }

    static juce::Result restoreSnapshot(DrumAnalysis& analysis, const juce::var& value, RestoredSnapshot& restored)
    {
        auto* root = value.getDynamicObject();
        if (root == nullptr) return juce::Result::fail("snapshot is not an object");
        auto* meta = root->getProperty("meta").getDynamicObject();
        if (meta == nullptr) return juce::Result::fail("meta block missing");
        juce::int64 schema = 0, version = 0, profile = 0;
        if (!readInteger(*meta, "schema", schema) || schema != snapshotSchema)
            return juce::Result::fail("unsupported snapshot schema");
        if (!readInteger(*meta, "analysisVersion", version) || version != analysisVersion)
            return juce::Result::fail("snapshot analysisVersion " + juce::String(version)
                + " differs from " + juce::String(analysisVersion) + ": regenerate the snapshot");
        readInteger(*meta, "profileVersion", profile);

        SnapshotContext context;
        double intensity = 0;
        if (!styleFromId(meta->getProperty("style").toString(), context.style)
            || !readNumber(*meta, "intensity", intensity) || !std::isfinite(intensity) || intensity < 0 || intensity > 1)
            return juce::Result::fail("invalid style or intensity");
        context.intensity = static_cast<float>(intensity);
        context.source = meta->getProperty("source").toString();
        context.licence = meta->getProperty("licence").toString();

        auto* input = root->getProperty("input").getDynamicObject();
        if (input == nullptr) return juce::Result::fail("input block missing");
        if (const auto result = settingsFromVar(input->getProperty("settings"), context.original); result.failed())
            return result;

        auto* recorded = root->getProperty("proposal").getDynamicObject();
        if (recorded == nullptr) return juce::Result::fail("proposal block missing");
        DrumProposal proposal;
        proposal.valid = static_cast<bool>(recorded->getProperty("valid"));
        proposal.summary = recorded->getProperty("summary").toString();
        double hits = 0, target = 0, maximum = 0;
        readNumber(*recorded, "hits", hits);
        readNumber(*recorded, "targetReduction", target);
        readNumber(*recorded, "maximumReduction", maximum);
        proposal.hits = static_cast<int>(hits);
        proposal.targetReduction = static_cast<float>(target);
        proposal.maximumReduction = static_cast<float>(maximum);
        if (!readFloats(recorded->getProperty("bandTargets"), proposal.bandTargets)
            || !readFloats(recorded->getProperty("bandCeilings"), proposal.bandCeilings))
            return juce::Result::fail("invalid band arrays");
        if (const auto result = settingsFromVar(recorded->getProperty("settings"), proposal.settings); result.failed())
            return result;

        if (const auto result = restoreAnalysis(analysis, root->getProperty("analysis")); result.failed())
            return result;
        restored.context = context;
        restored.recorded = proposal;
        return juce::Result::ok();
    }

    static juce::Result writeFile(const juce::File& file, const juce::var& snapshot)
    {
        if (!file.replaceWithText(juce::JSON::toString(snapshot, false)))
            return juce::Result::fail("cannot write " + file.getFullPathName());
        return juce::Result::ok();
    }

    static juce::Result readFile(const juce::File& file, juce::var& snapshot)
    {
        if (!file.existsAsFile()) return juce::Result::fail("missing file " + file.getFullPathName());
        return juce::JSON::parse(file.loadFileAsString(), snapshot);
    }

    // Development export: only active when built with ZED_ANALYSIS_EXPORT and the variable names a folder.
    static juce::File exportDirectory()
    {
        const auto path = juce::SystemStats::getEnvironmentVariable("ZED_ANALYSIS_EXPORT_DIR", {});
        return path.isEmpty() ? juce::File() : juce::File(path);
    }

    static juce::Result exportSnapshot(const DrumAnalysis& analysis, const SnapshotContext& context, const DrumProposal& proposal)
    {
        const auto directory = exportDirectory();
        if (directory == juce::File()) return juce::Result::ok();
        if (!directory.createDirectory()) return juce::Result::fail("cannot create " + directory.getFullPathName());
        const auto role = juce::File::createLegalFileName(instrumentName(instrumentFromValue(context.original[instrument])))
            .replaceCharacter(' ', '_');
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S")
            + juce::String::formatted("_%03d", static_cast<int>(juce::Time::getMillisecondCounter() % 1000));
        return writeFile(directory.getChildFile("snapshot_" + stamp + "_" + role + ".json"),
            toSnapshot(analysis, context, proposal));
    }

private:
    static bool isNumber(const juce::var& value) noexcept { return value.isInt() || value.isInt64() || value.isDouble(); }

    static bool readNumber(const juce::DynamicObject& object, const juce::Identifier& name, double& result)
    {
        const auto& value = object.getProperty(name);
        if (!isNumber(value)) return false;
        result = static_cast<double>(value);
        return true;
    }

    static bool readInteger(const juce::DynamicObject& object, const juce::Identifier& name, juce::int64& result)
    {
        const auto& value = object.getProperty(name);
        if (!value.isInt() && !value.isInt64()) return false;
        result = static_cast<juce::int64>(value);
        return true;
    }

    template <size_t N>
    static juce::Array<juce::var> counts(const std::array<int, N>& values)
    {
        juce::Array<juce::var> result;
        for (const auto value : values) result.add(value);
        return result;
    }

    template <size_t N>
    static juce::Array<juce::var> floats(const std::array<float, N>& values)
    {
        juce::Array<juce::var> result;
        for (const auto value : values) result.add(static_cast<double>(value));
        return result;
    }

    template <size_t N>
    static bool readCounts(const juce::var& value, std::array<int, N>& result)
    {
        const auto* array = value.getArray();
        if (array == nullptr || array->size() != static_cast<int>(N)) return false;
        for (size_t index = 0; index < N; ++index)
        {
            const auto& entry = (*array)[static_cast<int>(index)];
            if (!entry.isInt() && !entry.isInt64()) return false;
            const auto number = static_cast<juce::int64>(entry);
            if (number < 0 || number > std::numeric_limits<int>::max()) return false;
            result[index] = static_cast<int>(number);
        }
        return true;
    }

    template <size_t N>
    static bool readFloats(const juce::var& value, std::array<float, N>& result)
    {
        const auto* array = value.getArray();
        if (array == nullptr || array->size() != static_cast<int>(N)) return false;
        for (size_t index = 0; index < N; ++index)
        {
            const auto& entry = (*array)[static_cast<int>(index)];
            if (!isNumber(entry) || !std::isfinite(static_cast<double>(entry))) return false;
            result[index] = static_cast<float>(static_cast<double>(entry));
        }
        return true;
    }

    template <size_t N>
    static juce::String encode(const std::array<double, N>& values, size_t count)
    {
        juce::MemoryOutputStream stream;
        for (size_t index = 0; index < count; ++index) stream.writeDouble(values[index]);
        return juce::Base64::toBase64(stream.getData(), stream.getDataSize());
    }

    template <size_t N>
    static bool decode(const juce::String& text, std::array<double, N>& values, size_t count)
    {
        juce::MemoryOutputStream decoded;
        if (count > N || !juce::Base64::convertFromBase64(decoded, text)
            || decoded.getDataSize() != count * sizeof(double))
            return false;
        juce::MemoryInputStream stream(decoded.getData(), decoded.getDataSize(), false);
        values.fill(0);
        for (size_t index = 0; index < count; ++index)
        {
            const auto value = stream.readDouble();
            if (!std::isfinite(value) || value < 0) return false;
            values[index] = value;
        }
        return true;
    }
};
}
