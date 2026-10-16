# ZeroEffort Drum

Windows x64, C++17, JUCE 8.0.6, VST3 with ARA for Cubase. Cubase Gain was
an obsolete test and has been removed; do not recreate it or a Standalone target.
ZeroEffortDrum is the main CMake target, with no optional prototype switch.
Processor: Source/ARA/ZeroEffortDrumProcessor.cpp/.h. Shared modules are in
Source/Drum/Drum*.h; KickPresenceDSP.h is specific to Kick processing.
Editor and LookAndFeel: Source/UI. Multi-instance kit phase alignment:
Source/Kit (the audio thread only writes its own Member and runs KitAligner).

Use JUCE's audio, parameter and GUI APIs. Keep realtime processing free of
allocations, locks, file access and logging. Run the processor tests after audio
changes. Build VST3 with the windows-vs2019 preset using the existing
Build and Test Release task (scripts/Build.ps1 picks windows-vs2022, build dir
build-vs2022, when Visual Studio 2019 is not installed); three CTest suites cover cache, Drum and ARA
discovery. F5 runs the Debug tests, not an audio application. Preserve
parameter IDs and plugin codes unless intentionally breaking host compatibility.
Do not copy bundles to protected system directories automatically.
Do not launch audio playback without confirmation. Current guides:
docs/KitPhase10.md (kit phase, UI, architecture) and docs/DrumInstruments08.md
(instrument settings). Older docs describe historical prototype versions.