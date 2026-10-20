#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "HostOutput.h"
#include "../Drum/DrumEngine.h"
#include "../Drum/DrumAutoCapture.h"
#include "../Drum/DrumAutoPreset.h"
#include "../Drum/DrumAutoRadar.h"
#include "../Drum/DrumPresetLibrary.h"
#include "../Drum/DrumSpectrum.h"
#include "../Kit/KitRegistry.h"

class ZeroEffortDrumProcessor final : public juce::AudioProcessor,
                                     public juce::AudioProcessorARAExtension,
                                     public drum::HostOutputObserver,
                                     private juce::Timer
{
public:
    // Kit settings are drum::Settings controls: insert parameters, or values of the ARA audio
    // modifications like the instrument, so the ARA editor and playback instances agree.
    enum KitControl { kitGroup, kitMic, kitMode, kitStrength, kitPolarity, kitDelay, kitControlCount };
    static int kitSetting(KitControl control) noexcept { return drum::kitGroup + static_cast<int>(control); }

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
    // Auto radar (see DrumAutoRadar.h). With setAutoApplyWhenReady(false) a finished proposal waits for applyAutoPreset() or
    // discardAutoPreset() instead of being applied at once; the editor asks for it, headless use keeps the former behaviour.
    drum::AutoRadarView getAutoRadar() const;
    std::vector<drum::Change> getAutoChanges() const;
    void setAutoApplyWhenReady(bool apply) noexcept;
    bool discardAutoPreset();
    bool revertAutoPreset();
    bool autoPresetBusy() const;
    bool canApplyAutoPreset() const;
    bool canRevertAutoPreset() const;
    juce::String getAutoPresetText() const;
    // User presets: the sound of this instrument, stored per family (the four toms share theirs). Loading can be undone with revertAutoPreset().
    drum::PresetFamily getUserPresetFamily() const;
    std::vector<juce::String> listUserPresets() const;
    bool userPresetExists(const juce::String& name) const;
    juce::String getUserPresetHint(const juce::String& name) const;
    juce::Result saveUserPreset(const juce::String& name);
    juce::Result loadUserPreset(const juce::String& name);
    juce::Result deleteUserPreset(const juce::String& name);
    juce::File getUserPresetFolder() const;
    void setUserPresetRoot(const juce::File& folder);
    void setAutoStyle(drum::DrumStyle style, float intensity);
    drum::DrumStyle getAutoStyle() const;
    float getAutoIntensity() const;
    std::array<float, 4> getMeters() const noexcept;
    std::array<float, 3> getBandMeters() const noexcept;
    std::array<float, drum::finishMeterCount> getFinishMeters() const noexcept;
    drum::SpectrumMailbox& getSpectrumMailbox() noexcept;
    void recordHostOutput(float peak, int channels, bool active, bool fallback,
                          bool previouslySilent) noexcept override;
    void updateTrackProperties(const TrackProperties& properties) override;

    int currentInstrument() const;
    juce::String getTrackName() const { return trackName; }
    float getKitControl(KitControl control) const noexcept;
    void setKitControl(KitControl control, float value);
    kit::Mode getKitMode() const noexcept;
    kit::Identity getKitIdentity() const;
    kit::GroupView getKitView() const;
    // Read-only report of the microphones of this sub-bus (same Kit group, same instrument), empty for any other role.
    juce::String getGroupReportText() const;
    juce::uint32 getKitMemberId() const noexcept;
    void startKitAnalysis();
    void stopKitAnalysis();
    const kit::Recommendation* findOwnRecommendation(const kit::Report& report) const;
    void requestKitApply(const std::vector<juce::uint32>& members);
    void requestKitReset();
    bool canApplyKitRecommendation() const;
    bool applyKitRecommendation();
    void kitTick();

    juce::AudioProcessorValueTreeState parameters;

private:
    void timerCallback() override { kitTick(); }
    drum::Settings memberSettings() const;
    juce::String memberKey() const;
    juce::String selectionKey() const;
    void writeOwnKit(float delayMs, bool invert);
    juce::SharedResourcePointer<kit::Registry> kitRegistry;
    kit::Member* kitMember = nullptr;
    juce::String trackName;
    kit::Identity publishedIdentity;
    bool identityPublished = false;
    juce::uint32 kitAppliedRevision = 0;
    float kitAppliedStrength = -1;
    juce::uint32 kitApplySerial = 0;
    int kitApplyGroup = -1;
    double preparedRate = 0;

    struct AutoSession;
    std::unique_ptr<AutoSession> autoSession;
    drum::DrumAutoCapture autoCapture;
    drum::DrumPresetLibrary presetLibrary { drum::DrumPresetLibrary::defaultRoot() };
    juce::String autoTargetKey() const;
    juce::Result failPreset(const juce::String& message);
    void stepAutoPreset(bool measureRadar);
    static constexpr double autoRenderSliceMs = 22.0;   // of the 50 ms editor tick, left to the calibration and verification renders
    void writeSettings(const drum::Settings& settings);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    drum::DrumEngine engine;
    drum::SpectrumCapture spectrumCapture;
    drum::SpectrumMailbox spectrumMailbox;
    std::array<std::atomic<float>*, drum::controlCount> controls {};
    mutable std::vector<juce::String> lastEditedIds;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroEffortDrumProcessor)
};