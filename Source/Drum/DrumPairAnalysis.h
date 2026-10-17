#pragma once
#include "DrumAnalysisSnapshot.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <complex>

namespace drum
{
// Raw / mixed pair comparison (document 03, steps 1 and 2 plus the simple dynamics indices of 6.3 and 7.1). Offline only:
// it never runs in the audio path.
struct PairConstants
{
    // H: starting points to calibrate on pairs (document 03, 01-Q).
    static constexpr float coherenceReliable = 0.8f;      // bands under this are "unreliable" (5.2)
    static constexpr double minimumPlayingSeconds = 20;   // 4: at least 20 s of playing
    static constexpr float gainDriftDb = 3.0f;            // 5.3: level automation between the start and the end
    static constexpr float narrowPeakDb = 10.0f;          // 5.3: a peak this far above its neighbours is an alignment fault
    static constexpr float activeBelowPeakDb = 40.0f;     // frames of the raw take quieter than the loudest minus this are ignored
    static constexpr int frame = 4096;                    // same frame as the analysis
    static constexpr int coarseBlock = 44;                // about 1 ms at 44.1 kHz, used by the envelope correlation
    static constexpr int refineReach = 128;               // samples searched around the envelope lag
    static constexpr int refineLength = 1 << 19;          // samples used by the signal level correlation
};

struct RawMixResult
{
    bool aligned = false, accepted = false;
    juce::String reason;
    juce::StringArray warnings;
    double rate = 0, usedSeconds = 0, playingSeconds = 0;
    int lagSamples = 0;                 // mixed = raw delayed by this many samples (negative: the mix starts earlier)
    bool inverted = false;
    float alignCorrelation = 0;
    float gainDb = DrumAnalysis::unavailable();             // mean gain between 100 Hz and 5 kHz over the reliable bands
    float gainDriftDb = DrumAnalysis::unavailable();
    float medianCoherence = DrumAnalysis::unavailable();    // 100 Hz - 8 kHz
    float nonLinearDb = DrumAnalysis::unavailable();        // 7.2: level of what a linear filter of the raw does not explain
    std::array<float, thirdOctaveCount> shapeDb, coherence;
    std::array<bool, thirdOctaveCount> reliable {};
    float deltaCrestDb = DrumAnalysis::unavailable(), deltaActiveCrestDb = DrumAnalysis::unavailable();
    float deltaNoiseFloorDb = DrumAnalysis::unavailable();
    int hitsRaw = 0, hitsMix = 0;

    RawMixResult() { shapeDb.fill(DrumAnalysis::unavailable()); coherence.fill(DrumAnalysis::unavailable()); }
};

// A bus is not a processed track: it is the sum of several mics and of the mixer's treatment (document 03, 3.1 bis). The
// model is y = sum_i H_i x_i, estimated per frequency from the cross spectra (multiple regression, lightly regularised).
struct BusResult
{
    bool accepted = false;
    juce::String reason;
    juce::StringArray warnings;
    double rate = 0, usedSeconds = 0, playingSeconds = 0;
    std::vector<int> lagSamples;                 // per input: bus = input delayed by this many samples
    std::vector<bool> inverted;
    std::vector<float> alignCorrelation;
    std::vector<std::array<float, thirdOctaveCount>> gainDb;   // |H_i| per 1/3 octave band (level included)
    std::array<float, thirdOctaveCount> multipleCoherence;
    float medianCoherence = DrumAnalysis::unavailable();       // 100 Hz - 8 kHz
    BusResult() { multipleCoherence.fill(DrumAnalysis::unavailable()); }
};

class DrumPairAnalysis
{
public:
    static RawMixResult analyse(const std::vector<float>& raw, const std::vector<float>& mix, double rate)
    {
        RawMixResult result;
        result.rate = rate;
        if (!(rate >= 8000 && rate <= 192000)) { result.reason = "sample rate out of range"; return result; }
        if (raw.size() < static_cast<size_t>(rate * 2) || mix.size() < static_cast<size_t>(rate * 2)) { result.reason = "less than 2 s of audio"; return result; }

        const auto lag = findLag(raw, mix, rate, result);
        if (!result.aligned) return result;
        result.lagSamples = lag;

        const auto begin = static_cast<size_t>(juce::jmax(0, -lag));
        const auto end = juce::jmin(raw.size(), mix.size() - static_cast<size_t>(juce::jmax(0, lag)) + static_cast<size_t>(juce::jmax(0, -lag)));
        if (end <= begin + PairConstants::frame * 4) { result.aligned = false; result.reason = "the aligned signals overlap on less than a few frames"; return result; }
        std::vector<float> x(raw.begin() + static_cast<std::ptrdiff_t>(begin), raw.begin() + static_cast<std::ptrdiff_t>(end));
        std::vector<float> y(x.size());
        const auto sign = result.inverted ? -1.0f : 1.0f;
        for (size_t index = 0; index < x.size(); ++index)
            y[index] = sign * mix[static_cast<size_t>(static_cast<std::int64_t>(begin + index) + lag)];
        result.usedSeconds = static_cast<double>(x.size()) / rate;

        transferFunction(x, y, rate, result);
        dynamics(x, y, rate, result);
        verdict(result);
        return result;
    }

    // Bus of several mics: every input is aligned on the bus by its own best lag (the lags differ by the mic distances and by
    // what the mixer did with the phase), then the complex H_i absorb what is left.
    static BusResult analyseBus(const std::vector<std::vector<float>>& raws, const std::vector<float>& mix, double rate)
    {
        BusResult result;
        result.rate = rate;
        const auto count = raws.size();
        if (count < 1 || count > 4) { result.reason = "between 1 and 4 inputs are supported"; return result; }
        if (!(rate >= 8000 && rate <= 192000) || mix.size() < static_cast<size_t>(rate * 2)) { result.reason = "sample rate or length out of range"; return result; }

        std::vector<int> lags(count);
        std::vector<bool> flips(count);
        std::int64_t begin = 0, end = static_cast<std::int64_t>(mix.size());
        for (size_t input = 0; input < count; ++input)
        {
            RawMixResult alignment;
            lags[input] = findLag(raws[input], mix, rate, alignment);
            if (!alignment.aligned) { result.reason = "input " + juce::String(static_cast<int>(input) + 1) + ": " + alignment.reason; return result; }
            flips[input] = alignment.inverted;
            result.lagSamples.push_back(lags[input]);
            result.inverted.push_back(alignment.inverted);
            result.alignCorrelation.push_back(alignment.alignCorrelation);
            // bus index m reads input index m - lag
            begin = juce::jmax<std::int64_t>(begin, lags[input]);
            end = juce::jmin<std::int64_t>(end, static_cast<std::int64_t>(raws[input].size()) + lags[input]);
        }
        if (end - begin < PairConstants::frame * 8) { result.reason = "the aligned signals overlap on less than a few frames"; return result; }
        const auto length = static_cast<size_t>(end - begin);
        std::vector<std::vector<float>> x(count, std::vector<float>(length));
        for (size_t input = 0; input < count; ++input)
        {
            const auto sign = flips[input] ? -1.0f : 1.0f;
            for (size_t index = 0; index < length; ++index) x[input][index] = sign * raws[input][static_cast<size_t>(begin + static_cast<std::int64_t>(index) - lags[input])];
        }
        const std::vector<float> y(mix.begin() + begin, mix.begin() + end);
        result.usedSeconds = static_cast<double>(length) / rate;

        constexpr int size = PairConstants::frame;
        constexpr int hop = size / 2;
        constexpr size_t bins = size / 2;
        juce::dsp::FFT fft(nextPowerOfTwo(size));
        juce::dsp::WindowingFunction<float> window(size, juce::dsp::WindowingFunction<float>::hann);
        const auto frameCount = static_cast<int>((length - size) / hop) + 1;
        std::vector<float> levels(static_cast<size_t>(frameCount));
        for (int frame = 0; frame < frameCount; ++frame)
        {
            double energy = 0;
            for (int index = 0; index < size; ++index) { const double value = y[static_cast<size_t>(frame * hop + index)]; energy += value * value; }
            levels[static_cast<size_t>(frame)] = juce::Decibels::gainToDecibels(static_cast<float>(std::sqrt(energy / size)), -120.0f);
        }
        auto sorted = levels;
        std::sort(sorted.begin(), sorted.end());
        const auto threshold = sorted[sorted.size() * 95 / 100] - PairConstants::activeBelowPeakDb;

        using Cd = std::complex<double>;
        std::vector<std::vector<Cd>> sxx(bins, std::vector<Cd>(count * count));
        std::vector<std::vector<Cd>> sxy(bins, std::vector<Cd>(count));
        std::vector<double> syy(bins);
        std::vector<std::vector<float>> buffers(count + 1, std::vector<float>(static_cast<size_t>(size) * 2));
        int active = 0;
        for (int frame = 0; frame < frameCount; ++frame)
        {
            if (levels[static_cast<size_t>(frame)] < threshold) continue;
            for (size_t input = 0; input <= count; ++input)
            {
                auto& buffer = buffers[input];
                std::fill(buffer.begin(), buffer.end(), 0.0f);
                const auto& source = input < count ? x[input] : y;
                std::copy_n(source.begin() + frame * hop, size, buffer.begin());
                window.multiplyWithWindowingTable(buffer.data(), size);
                fft.performRealOnlyForwardTransform(buffer.data(), true);
            }
            for (size_t bin = 1; bin < bins; ++bin)
            {
                const Cd out(buffers[count][bin * 2], buffers[count][bin * 2 + 1]);
                syy[bin] += std::norm(out);
                for (size_t i = 0; i < count; ++i)
                {
                    const Cd a(buffers[i][bin * 2], buffers[i][bin * 2 + 1]);
                    sxy[bin][i] += std::conj(a) * out;
                    for (size_t j = 0; j < count; ++j)
                    {
                        const Cd b(buffers[j][bin * 2], buffers[j][bin * 2 + 1]);
                        sxx[bin][i * count + j] += std::conj(a) * b;
                    }
                }
            }
            ++active;
        }
        result.playingSeconds = static_cast<double>(active) * hop / rate;
        if (active < 4) { result.reason = "fewer than 4 active frames"; return result; }

        // Per bin: H = (Sxx + lambda I)^-1 Sxy, multiple coherence = Re(Sxy^H H) / Syy
        std::vector<std::vector<double>> magnitude(count, std::vector<double>(bins));
        std::vector<double> multiple(bins);
        for (size_t bin = 1; bin < bins; ++bin)
        {
            if (syy[bin] <= 0) continue;
            double trace = 0;
            for (size_t i = 0; i < count; ++i) trace += sxx[bin][i * count + i].real();
            if (trace <= 0) continue;
            std::vector<Cd> matrix = sxx[bin], rhs = sxy[bin];
            for (size_t i = 0; i < count; ++i) matrix[i * count + i] += 1.0e-4 * trace / static_cast<double>(count);
            // Gaussian elimination with partial pivoting
            bool singular = false;
            for (size_t column = 0; column < count && !singular; ++column)
            {
                size_t pivot = column;
                for (size_t row = column + 1; row < count; ++row)
                    if (std::abs(matrix[row * count + column]) > std::abs(matrix[pivot * count + column])) pivot = row;
                if (std::abs(matrix[pivot * count + column]) < 1.0e-30) { singular = true; break; }
                if (pivot != column) { for (size_t k = 0; k < count; ++k) std::swap(matrix[pivot * count + k], matrix[column * count + k]); std::swap(rhs[pivot], rhs[column]); }
                for (size_t row = column + 1; row < count; ++row)
                {
                    const auto factor = matrix[row * count + column] / matrix[column * count + column];
                    for (size_t k = column; k < count; ++k) matrix[row * count + k] -= factor * matrix[column * count + k];
                    rhs[row] -= factor * rhs[column];
                }
            }
            if (singular) continue;
            std::vector<Cd> h(count);
            for (size_t row = count; row-- > 0;)
            {
                auto value = rhs[row];
                for (size_t k = row + 1; k < count; ++k) value -= matrix[row * count + k] * h[k];
                h[row] = value / matrix[row * count + row];
            }
            Cd explained = 0;
            for (size_t i = 0; i < count; ++i) { explained += std::conj(sxy[bin][i]) * h[i]; magnitude[i][bin] = std::abs(h[i]); }
            multiple[bin] = juce::jlimit(0.0, 1.0, explained.real() / syy[bin]);
        }

        result.gainDb.assign(count, {});
        for (auto& values : result.gainDb) values.fill(DrumAnalysis::unavailable());
        const auto limit = rate * MeasureConstants::bandLimitRatio;
        std::vector<float> inRange;
        for (int band = 0; band < thirdOctaveCount; ++band)
        {
            const auto centre = thirdOctaveCentre(band);
            const auto lower = centre / std::pow(2.0, 1.0 / 6.0), upper = centre * std::pow(2.0, 1.0 / 6.0);
            if (upper > limit || lower < 2.0 * rate / size) continue;
            std::vector<double> sums(count);
            double coherenceSum = 0;
            int used = 0;
            for (size_t bin = 1; bin < bins; ++bin)
            {
                const auto frequency = static_cast<double>(bin) * rate / size;
                if (frequency < lower || frequency >= upper || multiple[bin] <= 0) continue;
                for (size_t i = 0; i < count; ++i) sums[i] += magnitude[i][bin];
                coherenceSum += multiple[bin];
                ++used;
            }
            if (used < 1) continue;
            for (size_t i = 0; i < count; ++i)
                if (sums[i] > 0) result.gainDb[i][static_cast<size_t>(band)] = static_cast<float>(20 * std::log10(sums[i] / used));
            result.multipleCoherence[static_cast<size_t>(band)] = static_cast<float>(coherenceSum / used);
            if (centre >= 100 && centre <= 8000) inRange.push_back(result.multipleCoherence[static_cast<size_t>(band)]);
        }
        if (!inRange.empty()) { std::sort(inRange.begin(), inRange.end()); result.medianCoherence = inRange[inRange.size() / 2]; }
        result.accepted = std::isfinite(result.medianCoherence) && result.medianCoherence >= PairConstants::coherenceReliable
            && result.playingSeconds >= PairConstants::minimumPlayingSeconds;
        if (!result.accepted)
            result.reason = !std::isfinite(result.medianCoherence) ? "coherence unavailable"
                : result.medianCoherence < PairConstants::coherenceReliable
                    ? "multiple coherence " + juce::String(result.medianCoherence, 2) + " under 0.8 : the bus holds more than these inputs (other mics, samples, reverb) or the inputs are incomplete"
                    : "only " + juce::String(result.playingSeconds, 1) + " s of playing (20 s needed)";
        return result;
    }

    static juce::String toMarkdown(const BusResult& result, const juce::StringArray& names, const juce::String& title)
    {
        juce::String text;
        text << "# Bus / micros : " << title << "\n\n" << (result.accepted ? "**Bus explique par ses micros**" : "**Bus non explique** : " + result.reason) << "\n\n";
        for (size_t input = 0; input < result.lagSamples.size(); ++input)
            text << "- " << names[static_cast<int>(input)] << " : decalage " << result.lagSamples[input] << " echantillons ("
                 << juce::String(1000.0 * result.lagSamples[input] / result.rate, 2) << " ms)" << (result.inverted[input] ? ", **polarite inversee**" : "")
                 << ", correlation " << juce::String(result.alignCorrelation[input], 2) << "\n";
        text << "- Duree commune " << juce::String(result.usedSeconds, 1) << " s, jeu " << juce::String(result.playingSeconds, 1)
             << " s, coherence multiple mediane " << fixed(result.medianCoherence, 2) << "\n\n| Bande (Hz) | Coherence multiple |";
        for (const auto& name : names) text << " " << name << " (dB) |";
        text << "\n|---|---|" << juce::String::repeatedString("---|", names.size()) << "\n";
        for (int band = 0; band < thirdOctaveCount; ++band)
        {
            if (!std::isfinite(result.multipleCoherence[static_cast<size_t>(band)])) continue;
            text << "| " << DrumAnalysisSnapshot::thirdOctaveLabel(band) << " | " << fixed(result.multipleCoherence[static_cast<size_t>(band)], 2) << " |";
            for (size_t input = 0; input < result.gainDb.size(); ++input) text << " " << fixed(result.gainDb[input][static_cast<size_t>(band)]) << " |";
            text << "\n";
        }
        return text;
    }

    static juce::var toVar(const BusResult& result, const juce::StringArray& names)
    {
        const auto number = [](double value) { return std::isfinite(value) ? juce::var(value) : juce::var(); };
        auto* object = new juce::DynamicObject();
        object->setProperty("accepted", result.accepted);
        object->setProperty("reason", result.reason);
        object->setProperty("rate", result.rate);
        object->setProperty("used_seconds", result.usedSeconds);
        object->setProperty("playing_seconds", result.playingSeconds);
        object->setProperty("coherence_multiple_mediane", number(result.medianCoherence));
        juce::Array<juce::var> inputs, coherence;
        for (size_t input = 0; input < result.lagSamples.size(); ++input)
        {
            auto* entry = new juce::DynamicObject();
            entry->setProperty("nom", names[static_cast<int>(input)]);
            entry->setProperty("lag_samples", result.lagSamples[input]);
            entry->setProperty("lag_ms", 1000.0 * result.lagSamples[input] / result.rate);
            entry->setProperty("inverted", static_cast<bool>(result.inverted[input]));
            entry->setProperty("align_correlation", number(result.alignCorrelation[input]));
            juce::Array<juce::var> gains;
            for (const auto value : result.gainDb[input]) gains.add(number(value));
            entry->setProperty("gain_db_1_3_octave", gains);
            inputs.add(juce::var(entry));
        }
        for (const auto value : result.multipleCoherence) coherence.add(number(value));
        object->setProperty("entrees", inputs);
        object->setProperty("coherence_multiple_1_3_octave", coherence);
        return juce::var(object);
    }

    static juce::var toVar(const RawMixResult& result)
    {
        const auto number = [](double value) { return std::isfinite(value) ? juce::var(value) : juce::var(); };
        auto* object = new juce::DynamicObject();
        object->setProperty("aligned", result.aligned);
        object->setProperty("accepted", result.accepted);
        object->setProperty("reason", result.reason);
        object->setProperty("rate", result.rate);
        object->setProperty("used_seconds", result.usedSeconds);
        object->setProperty("playing_seconds", result.playingSeconds);
        object->setProperty("lag_samples", result.lagSamples);
        object->setProperty("lag_ms", result.rate > 0 ? 1000.0 * result.lagSamples / result.rate : 0.0);
        object->setProperty("inverted", result.inverted);
        object->setProperty("align_correlation", number(result.alignCorrelation));
        object->setProperty("gain_moyen_db", number(result.gainDb));
        object->setProperty("gain_drift_db", number(result.gainDriftDb));
        object->setProperty("coherence_mediane", number(result.medianCoherence));
        object->setProperty("couleur_non_lineaire_db", number(result.nonLinearDb));
        object->setProperty("delta_crest_db", number(result.deltaCrestDb));
        object->setProperty("delta_crest_actif_db", number(result.deltaActiveCrestDb));
        object->setProperty("delta_plancher_db", number(result.deltaNoiseFloorDb));
        object->setProperty("hits_raw", result.hitsRaw);
        object->setProperty("hits_mix", result.hitsMix);
        juce::Array<juce::var> shape, coherence, reliable;
        for (int band = 0; band < thirdOctaveCount; ++band)
        {
            shape.add(number(result.shapeDb[static_cast<size_t>(band)]));
            coherence.add(number(result.coherence[static_cast<size_t>(band)]));
            reliable.add(result.reliable[static_cast<size_t>(band)]);
        }
        object->setProperty("forme_db_1_3_octave", shape);
        object->setProperty("coherence_1_3_octave", coherence);
        object->setProperty("bande_fiable", reliable);
        juce::Array<juce::var> warnings;
        for (const auto& warning : result.warnings) warnings.add(warning);
        object->setProperty("avertissements", warnings);
        return juce::var(object);
    }

    static juce::String toMarkdown(const RawMixResult& result, const juce::String& title)
    {
        juce::String text;
        text << "# Paire brut / mixe : " << title << "\n\n";
        if (!result.aligned) return text << "**Alignement impossible** : " << result.reason << "\n";
        text << (result.accepted ? "**Paire acceptee**" : "**Paire ecartee** : " + result.reason) << "\n\n";
        text << "- Decalage : " << result.lagSamples << " echantillons (" << juce::String(1000.0 * result.lagSamples / result.rate, 2) << " ms)"
             << (result.inverted ? ", **polarite inversee**" : "") << ", correlation " << juce::String(result.alignCorrelation, 2) << "\n";
        text << "- Duree commune " << juce::String(result.usedSeconds, 1) << " s, jeu " << juce::String(result.playingSeconds, 1) << " s\n";
        text << "- Gain moyen 100 Hz - 5 kHz : " << fixed(result.gainDb) << " dB, derive " << fixed(result.gainDriftDb) << " dB\n";
        text << "- Coherence mediane 100 Hz - 8 kHz : " << fixed(result.medianCoherence, 2) << ", couleur non lineaire " << fixed(result.nonLinearDb) << " dB\n";
        text << "- Crest brut -> mixe : " << fixed(result.deltaCrestDb) << " dB (actif " << fixed(result.deltaActiveCrestDb) << " dB), plancher de bruit "
             << fixed(result.deltaNoiseFloorDb) << " dB, frappes " << result.hitsRaw << " -> " << result.hitsMix << "\n";
        for (const auto& warning : result.warnings) text << "- Attention : " << warning << "\n";
        text << "\n| Bande (Hz) | Forme (dB) | Coherence | Fiable |\n|---|---|---|---|\n";
        for (int band = 0; band < thirdOctaveCount; ++band)
            if (std::isfinite(result.shapeDb[static_cast<size_t>(band)]))
                text << "| " << DrumAnalysisSnapshot::thirdOctaveLabel(band) << " | " << fixed(result.shapeDb[static_cast<size_t>(band)]) << " | "
                     << fixed(result.coherence[static_cast<size_t>(band)], 2) << " | " << (result.reliable[static_cast<size_t>(band)] ? "oui" : "non") << " |\n";
        return text;
    }

private:
    static juce::String fixed(double value, int decimals = 1)
    {
        return std::isfinite(value) ? juce::String(value, decimals) : juce::String("-");
    }

    using Complex = std::complex<float>;

    static int nextPowerOfTwo(size_t value)
    {
        int order = 1;
        while ((static_cast<size_t>(1) << order) < value) ++order;
        return order;
    }

    // c(m) = sum_n a[n] * b[n + m] for m in [firstLag, lastLag]; the result is indexed by m - firstLag.
    static std::vector<float> crossCorrelation(const std::vector<float>& a, const std::vector<float>& b, int firstLag, int lastLag)
    {
        const auto order = nextPowerOfTwo(a.size() + b.size());
        const auto size = static_cast<size_t>(1) << order;
        juce::dsp::FFT fft(order);
        std::vector<Complex> left(size), right(size), spectrum(size), output(size);
        for (size_t index = 0; index < a.size(); ++index) left[index] = a[index];
        for (size_t index = 0; index < b.size(); ++index) right[index] = b[index];
        fft.perform(left.data(), spectrum.data(), false);
        std::swap(left, spectrum);
        fft.perform(right.data(), spectrum.data(), false);
        for (size_t index = 0; index < size; ++index) left[index] = std::conj(left[index]) * spectrum[index];
        fft.perform(left.data(), output.data(), true);
        std::vector<float> values(static_cast<size_t>(lastLag - firstLag + 1));
        for (int lag = firstLag; lag <= lastLag; ++lag)
        {
            const auto index = lag >= 0 ? static_cast<size_t>(lag) : size - static_cast<size_t>(-lag);
            values[static_cast<size_t>(lag - firstLag)] = output[index].real();
        }
        return values;
    }

    static std::vector<float> envelope(const std::vector<float>& signal, int block)
    {
        std::vector<float> values(signal.size() / static_cast<size_t>(block));
        for (size_t index = 0; index < values.size(); ++index)
        {
            double energy = 0;
            for (int offset = 0; offset < block; ++offset)
            {
                const double value = signal[index * static_cast<size_t>(block) + static_cast<size_t>(offset)];
                energy += value * value;
            }
            values[index] = static_cast<float>(std::sqrt(energy / block));
        }
        double mean = 0;
        for (const auto value : values) mean += value;
        mean /= static_cast<double>(juce::jmax<size_t>(1, values.size()));
        for (auto& value : values) value -= static_cast<float>(mean);
        return values;
    }

    // Step 1: coarse lag from the 1 ms envelopes over the whole files, then the exact lag and the polarity from the signals
    // on the loudest stretch. Positive lag: the mix is later than the raw take.
    static int findLag(const std::vector<float>& raw, const std::vector<float>& mix, double rate, RawMixResult& result)
    {
        const auto block = juce::jmax(1, static_cast<int>(std::lround(PairConstants::coarseBlock * rate / 44100.0)));
        const auto envelopeRaw = envelope(raw, block), envelopeMix = envelope(mix, block);
        const auto lastBlockLag = static_cast<int>(envelopeMix.size()) - 1;
        const auto firstBlockLag = -(static_cast<int>(envelopeRaw.size()) - 1);
        const auto coarse = crossCorrelation(envelopeRaw, envelopeMix, firstBlockLag, lastBlockLag);
        int best = 0;
        for (size_t index = 1; index < coarse.size(); ++index)
            if (coarse[index] > coarse[static_cast<size_t>(best)]) best = static_cast<int>(index);
        const auto coarseLag = (firstBlockLag + best) * block;

        // Loudest stretch of the raw take inside the overlap
        const auto reach = PairConstants::refineReach + block;
        const auto overlapStart = static_cast<std::int64_t>(juce::jmax(0, -coarseLag)) + reach;
        const auto overlapEnd = static_cast<std::int64_t>(juce::jmin<std::int64_t>(static_cast<std::int64_t>(raw.size()),
            static_cast<std::int64_t>(mix.size()) - coarseLag)) - reach;
        const auto length = juce::jmin<std::int64_t>(PairConstants::refineLength, overlapEnd - overlapStart);
        if (length < PairConstants::frame * 8) { result.reason = "the files do not overlap enough after the coarse alignment"; return 0; }
        std::int64_t start = overlapStart;
        double bestEnergy = -1;
        const auto stride = static_cast<std::int64_t>(length / 4);
        for (auto candidate = overlapStart; candidate + length <= overlapEnd; candidate += juce::jmax<std::int64_t>(1, stride))
        {
            double energy = 0;
            for (auto index = candidate; index < candidate + length; index += 16) energy += static_cast<double>(raw[static_cast<size_t>(index)]) * raw[static_cast<size_t>(index)];
            if (energy > bestEnergy) { bestEnergy = energy; start = candidate; }
        }
        const std::vector<float> a(raw.begin() + start, raw.begin() + start + length);
        const auto from = start + coarseLag - reach;
        const std::vector<float> b(mix.begin() + from, mix.begin() + from + length + 2 * reach);
        const auto fine = crossCorrelation(a, b, 0, 2 * reach);
        size_t peak = 0;
        for (size_t index = 1; index < fine.size(); ++index)
            if (std::abs(fine[index]) > std::abs(fine[peak])) peak = index;
        const auto lag = coarseLag + static_cast<int>(peak) - reach;

        double sumXY = 0, sumXX = 0, sumYY = 0;
        for (std::int64_t index = 0; index < length; ++index)
        {
            const double xv = a[static_cast<size_t>(index)];
            const double yv = mix[static_cast<size_t>(start + index + lag)];
            sumXY += xv * yv; sumXX += xv * xv; sumYY += yv * yv;
        }
        result.aligned = true;
        result.inverted = sumXY < 0;
        result.alignCorrelation = static_cast<float>(sumXX > 0 && sumYY > 0 ? std::abs(sumXY) / std::sqrt(sumXX * sumYY) : 0.0);
        return lag;
    }

    // Step 2: Welch estimate of H = Sxy / Sxx and of the coherence, folded into 1/3 octave bands.
    static void transferFunction(const std::vector<float>& x, const std::vector<float>& y, double rate, RawMixResult& result)
    {
        constexpr int size = PairConstants::frame;
        constexpr int hop = size / 2;
        juce::dsp::FFT fft(nextPowerOfTwo(size));
        juce::dsp::WindowingFunction<float> window(size, juce::dsp::WindowingFunction<float>::hann);
        const auto frameCount = static_cast<int>((x.size() - size) / hop) + 1;
        std::vector<float> levels(static_cast<size_t>(frameCount));
        for (int frame = 0; frame < frameCount; ++frame)
        {
            double energy = 0;
            for (int index = 0; index < size; ++index)
            {
                const double value = x[static_cast<size_t>(frame * hop + index)];
                energy += value * value;
            }
            levels[static_cast<size_t>(frame)] = juce::Decibels::gainToDecibels(static_cast<float>(std::sqrt(energy / size)), -120.0f);
        }
        auto sorted = levels;
        std::sort(sorted.begin(), sorted.end());
        const auto top = sorted[static_cast<size_t>(sorted.size() * 95 / 100)];
        const auto threshold = top - PairConstants::activeBelowPeakDb;

        const auto bins = static_cast<size_t>(size / 2);
        std::vector<double> sxx(bins), syy(bins);
        std::vector<std::complex<double>> sxy(bins);
        std::vector<float> bufferX(static_cast<size_t>(size) * 2), bufferY(static_cast<size_t>(size) * 2);
        std::vector<float> frameGain;
        int active = 0;
        const auto lowBin = static_cast<size_t>(100.0 * size / rate), highBin = static_cast<size_t>(5000.0 * size / rate);
        for (int frame = 0; frame < frameCount; ++frame)
        {
            if (levels[static_cast<size_t>(frame)] < threshold) continue;
            std::fill(bufferX.begin(), bufferX.end(), 0.0f);
            std::fill(bufferY.begin(), bufferY.end(), 0.0f);
            std::copy_n(x.begin() + frame * hop, size, bufferX.begin());
            std::copy_n(y.begin() + frame * hop, size, bufferY.begin());
            window.multiplyWithWindowingTable(bufferX.data(), size);
            window.multiplyWithWindowingTable(bufferY.data(), size);
            fft.performRealOnlyForwardTransform(bufferX.data(), true);
            fft.performRealOnlyForwardTransform(bufferY.data(), true);
            double bandX = 0, bandY = 0;
            for (size_t bin = 1; bin < bins; ++bin)
            {
                const std::complex<double> a(bufferX[bin * 2], bufferX[bin * 2 + 1]), b(bufferY[bin * 2], bufferY[bin * 2 + 1]);
                sxx[bin] += std::norm(a);
                syy[bin] += std::norm(b);
                sxy[bin] += std::conj(a) * b;
                if (bin >= lowBin && bin <= highBin) { bandX += std::norm(a); bandY += std::norm(b); }
            }
            if (bandX > 0 && bandY > 0) frameGain.push_back(static_cast<float>(10 * std::log10(bandY / bandX)));
            ++active;
        }
        result.playingSeconds = static_cast<double>(active) * hop / rate;
        if (active < 4) { result.warnings.add("fewer than 4 active frames"); return; }

        // Level automation: median frame gain over the first and the last third of the active frames
        if (frameGain.size() >= 12)
        {
            const auto third = frameGain.size() / 3;
            const auto median = [](std::vector<float> values) { std::sort(values.begin(), values.end()); return values[values.size() / 2]; };
            result.gainDriftDb = std::abs(median({ frameGain.end() - static_cast<std::ptrdiff_t>(third), frameGain.end() })
                - median({ frameGain.begin(), frameGain.begin() + static_cast<std::ptrdiff_t>(third) }));
        }

        const auto limit = rate * MeasureConstants::bandLimitRatio;
        double colourNumerator = 0, colourDenominator = 0;
        std::array<float, thirdOctaveCount> gain;
        gain.fill(DrumAnalysis::unavailable());
        for (int band = 0; band < thirdOctaveCount; ++band)
        {
            const auto centre = thirdOctaveCentre(band);
            const auto lower = centre / std::pow(2.0, 1.0 / 6.0), upper = centre * std::pow(2.0, 1.0 / 6.0);
            if (upper > limit || lower < 2.0 * rate / size) continue;
            double magnitude = 0, power = 0, coherenceSum = 0;
            int count = 0;
            for (size_t bin = 1; bin < bins; ++bin)
            {
                const auto frequency = static_cast<double>(bin) * rate / size;
                if (frequency < lower || frequency >= upper || sxx[bin] <= 0 || syy[bin] <= 0) continue;
                magnitude += std::abs(sxy[bin]);
                power += sxx[bin];
                coherenceSum += std::norm(sxy[bin]) / (sxx[bin] * syy[bin]);
                ++count;
                if (frequency >= 100 && frequency <= 8000)
                {
                    colourNumerator += (1.0 - std::norm(sxy[bin]) / (sxx[bin] * syy[bin])) * syy[bin];
                    colourDenominator += syy[bin];
                }
            }
            if (count < 1 || power <= 0 || magnitude <= 0) continue;
            gain[static_cast<size_t>(band)] = static_cast<float>(20 * std::log10(magnitude / power));
            result.coherence[static_cast<size_t>(band)] = static_cast<float>(coherenceSum / count);
            result.reliable[static_cast<size_t>(band)] = result.coherence[static_cast<size_t>(band)] >= PairConstants::coherenceReliable;
        }
        if (colourDenominator > 0) result.nonLinearDb = static_cast<float>(10 * std::log10(juce::jmax(1.0e-12, colourNumerator / colourDenominator)));

        std::vector<float> coherenceInRange;
        double sum = 0;
        int counted = 0, reliableCounted = 0;
        for (int band = 0; band < thirdOctaveCount; ++band)
        {
            const auto centre = thirdOctaveCentre(band);
            if (!std::isfinite(gain[static_cast<size_t>(band)])) continue;
            if (centre >= 100 && centre <= 8000) coherenceInRange.push_back(result.coherence[static_cast<size_t>(band)]);
            if (centre >= 100 && centre <= 5000) { sum += gain[static_cast<size_t>(band)]; ++counted; if (result.reliable[static_cast<size_t>(band)]) ++reliableCounted; }
        }
        if (!coherenceInRange.empty())
        {
            std::sort(coherenceInRange.begin(), coherenceInRange.end());
            result.medianCoherence = coherenceInRange[coherenceInRange.size() / 2];
        }
        // The mean gain only uses the reliable bands; with none, it falls back on all of them
        sum = 0;
        counted = 0;
        for (int band = 0; band < thirdOctaveCount; ++band)
        {
            const auto centre = thirdOctaveCentre(band);
            if (!std::isfinite(gain[static_cast<size_t>(band)]) || centre < 100 || centre > 5000) continue;
            if (reliableCounted > 0 && !result.reliable[static_cast<size_t>(band)]) continue;
            sum += gain[static_cast<size_t>(band)];
            ++counted;
        }
        if (counted > 0) result.gainDb = static_cast<float>(sum / counted);
        for (int band = 0; band < thirdOctaveCount; ++band)
            if (std::isfinite(gain[static_cast<size_t>(band)]) && std::isfinite(result.gainDb))
                result.shapeDb[static_cast<size_t>(band)] = gain[static_cast<size_t>(band)] - result.gainDb;
    }

    // 6.3 and 7.1: simple indices on the aligned signals (the mixed one keeps its own level: the indices do not depend on it)
    static void dynamics(const std::vector<float>& x, const std::vector<float>& y, double rate, RawMixResult& result)
    {
        const auto measure = [&](const std::vector<float>& signal) {
            auto analyser = std::make_unique<DrumAnalyser>();
            analyser->prepare(rate);
            for (size_t offset = 0; offset < signal.size(); offset += 4096)
                analyser->push(signal.data() + offset, static_cast<int>(juce::jmin<size_t>(4096, signal.size() - offset)));
            return analyser;
        };
        const auto before = measure(x);
        const auto after = measure(y);
        const auto& a = before->analysis();
        const auto& b = after->analysis();
        result.deltaCrestDb = b.crestDb() - a.crestDb();
        result.deltaActiveCrestDb = b.activeCrestDb() - a.activeCrestDb();
        // The noise floor of the mix is read after removing the gain, so that a level change is not taken for a gate
        result.deltaNoiseFloorDb = (b.noiseFloorDb() - a.noiseFloorDb()) - (std::isfinite(result.gainDb) ? result.gainDb : 0.0f);
        result.hitsRaw = a.hitCount();
        result.hitsMix = b.hitCount();
    }

    static void verdict(RawMixResult& result)
    {
        result.accepted = true;
        const auto reject = [&](const juce::String& reason) { if (result.accepted) result.reason = reason; result.accepted = false; };
        if (result.alignCorrelation < 0.1f) reject("alignment correlation " + juce::String(result.alignCorrelation, 2) + " : the two files do not look like the same take");
        if (result.playingSeconds < PairConstants::minimumPlayingSeconds) reject("only " + juce::String(result.playingSeconds, 1) + " s of playing (20 s needed)");
        if (!std::isfinite(result.medianCoherence)) reject("coherence unavailable");
        else if (result.medianCoherence < PairConstants::coherenceReliable)
            reject("median coherence " + juce::String(result.medianCoherence, 2) + " under " + juce::String(PairConstants::coherenceReliable, 1)
                + " : too much was added (reverb, samples, parallel) or the files are not the same signal");
        if (std::isfinite(result.gainDriftDb) && result.gainDriftDb > PairConstants::gainDriftDb)
            result.warnings.add("level drift of " + juce::String(result.gainDriftDb, 1) + " dB between the start and the end (automation)");
        for (int band = 1; band + 1 < thirdOctaveCount; ++band)
        {
            const auto i = static_cast<size_t>(band);
            if (!result.reliable[i] || !std::isfinite(result.shapeDb[i - 1]) || !std::isfinite(result.shapeDb[i + 1])) continue;
            if (result.shapeDb[i] - 0.5f * (result.shapeDb[i - 1] + result.shapeDb[i + 1]) > PairConstants::narrowPeakDb)
            {
                result.warnings.add("narrow peak of more than 10 dB at " + DrumAnalysisSnapshot::thirdOctaveLabel(band) + " Hz (alignment fault or comb, not an EQ)");
                break;
            }
        }
    }
};
}
