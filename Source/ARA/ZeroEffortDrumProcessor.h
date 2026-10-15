#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "HostOutput.h"
#include "../Drum/DrumEngine.h"
#include "../Drum/DrumAutoCapture.h"
#include "../Drum/DrumAutoPreset.h"
#include "../Drum/DrumSpectrum.h"

class ZeroEffortDrumProcessor final : public juce::AudioProcessor,
                                     public juce::AudioProcessorARAExtension,
                                     public drum::HostOutputObserver
{
public:
    ZeroEffortDrumProcessor();
    ~ZeroEffortDrumProcessor() override;
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ZeroEffort Drum"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;
    juce::String getDiagnosticText() const;
    drum::Settings getSettings() const;
    bool canEditSettings() const;
    bool canBypass() const;
    void setControl(int index, float value);
    void startAutoPreset();
    void updateAutoPreset();
    void cancelAutoPreset();
    bool applyAutoPreset();
    bool revertAutoPreset();
    bool autoPresetBusy() const;
    bool canApplyAutoPreset() const;
    bool canRevertAutoPreset() const;
    juce::String getAutoPresetText() const;
    void setAutoStyle(drum::DrumStyle style, float intensity);
    drum::DrumStyle getAutoStyle() const;
    float getAutoIntensity() const;
    std::array<float, 4> getMeters() const noexcept;
    std::array<float, 3> getBandMeters() const noexcept;
    std::array<float, drum::finishMeterCount> getFinishMeters() const noexcept;
    drum::SpectrumMailbox& getSpectrumMailbox() noexcept;
    void recordHostOutput(float peak, int channels, bool active, bool fallback,
                          bool previouslySilent) noexcept override;

    juce::AudioProcessorValueTreeState parameters;

private:
    struct AutoSession;
    std::unique_ptr<AutoSession> autoSession;
    drum::DrumAutoCapture autoCapture;
    juce::String autoTargetKey() const;
    void writeSettings(const drum::Settings& settings);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    drum::DrumEngine engine;
    drum::SpectrumCapture spectrumCapture;
    drum::SpectrumMailbox spectrumMailbox;
    std::array<std::atomic<float>*, drum::controlCount> controls {};
    mutable std::vector<juce::String> lastEditedIds;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroEffortDrumProcessor)
};