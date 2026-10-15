#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ZeroEffortDrumProcessor;

namespace drum::ui
{
juce::AudioProcessorEditor* createEditor(ZeroEffortDrumProcessor& processor);
}
