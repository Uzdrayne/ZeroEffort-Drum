function(patch_juce_ara original replacement)
    file(READ "${wrapper}" source)
    string(FIND "${source}" "${replacement}" patched)
    if(NOT patched EQUAL -1)
        return()
    endif()
    string(FIND "${source}" "${original}" anchor)
    if(anchor EQUAL -1)
        message(FATAL_ERROR "JUCE ARA bus workaround anchor changed; review the pinned JUCE wrapper")
    endif()
    string(REPLACE "${original}" "${replacement}" source "${source}")
    file(WRITE "${wrapper}" "${source}")
endfunction()

set(wrapper "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
patch_juce_ara([=[#include <juce_core/system/juce_TargetPlatform.h>]=]
    [=[#include <juce_core/system/juce_TargetPlatform.h>
#if defined (DRUM_ARA_CUBASE_BUS_WORKAROUND)
 #include "HostOutput.h"
#endif]=])
patch_juce_ara([=[    ClientBufferMapper bufferMapper;]=]
    [=[    ClientBufferMapper bufferMapper;
   #if defined (DRUM_ARA_CUBASE_BUS_WORKAROUND) && JucePlugin_Enable_ARA
    drum::HostBusActivation hostBusActivation;
    bool receivedAudioBusActivation = false;
   #endif]=])

patch_juce_ara([=[            if (dir == Vst::kInput)
                bufferMapper.setInputBusHostActive ((size_t) index, state != 0);]=]
    [=[           #if defined (DRUM_ARA_CUBASE_BUS_WORKAROUND) && JucePlugin_Enable_ARA
            receivedAudioBusActivation = true;
            hostBusActivation.receive (dir == Vst::kInput);
           #endif
            if (dir == Vst::kInput)
                bufferMapper.setInputBusHostActive ((size_t) index, state != 0);]=])

patch_juce_ara([=[            bufferMapper.updateFromProcessor (*pluginInstance);
            return kResultTrue;
        }

        // apply layout changes in reverse order]=]
    [=[            bufferMapper.updateFromProcessor (*pluginInstance);
           #if defined (DRUM_ARA_CUBASE_BUS_WORKAROUND) && JucePlugin_Enable_ARA
            if (! receivedAudioBusActivation
                && (detail::PluginUtilities::getHostType().isCubase() || detail::PluginUtilities::getHostType().isNuendo()))
                if (auto* extension = dynamic_cast<AudioProcessorARAExtension*> (pluginInstance))
                    if (extension->isBoundToARA())
                    {
                        for (int bus = 0; bus < numIns; ++bus)
                            bufferMapper.setInputBusHostActive ((size_t) bus, inputs[bus] != 0);
                        for (int bus = 0; bus < numOuts; ++bus)
                            bufferMapper.setOutputBusHostActive ((size_t) bus, outputs[bus] != 0);
                    }
           #endif
            return kResultTrue;
        }

        // apply layout changes in reverse order]=])

patch_juce_ara([=[        ClientRemappedBuffer<FloatType> remappedBuffer { bufferMapper, data };]=]
    [=[       #if defined (DRUM_ARA_CUBASE_BUS_WORKAROUND) && JucePlugin_Enable_ARA
        auto* extension = dynamic_cast<AudioProcessorARAExtension*> (pluginInstance);
        const auto fallback = hostBusActivation.synchronise (bufferMapper,
            extension != nullptr && extension->isBoundToARA(),
            detail::PluginUtilities::getHostType().isCubase() || detail::PluginUtilities::getHostType().isNuendo());
        drum::HostOutputProbe<FloatType, Vst::ProcessData> outputProbe { data,
            dynamic_cast<drum::HostOutputObserver*> (pluginInstance),
            ! bufferMapper.getOutputMap().empty() && bufferMapper.getOutputMap().front().isHostActive(), fallback };
       #endif
        ClientRemappedBuffer<FloatType> remappedBuffer { bufferMapper, data };]=])