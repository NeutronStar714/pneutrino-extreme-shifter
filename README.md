# Extreme Shifter

Extreme Shifter is a three-voice polyphonic pitch shifter from NeutronStar714's Pneutrino series. It processes chords and single notes for live guitar, harmonies, and pitch sweeps. Each voice has independent semitone, fine tuning, tone, and balance controls.

Use **Step** mode to choose semitone intervals or **Smooth** mode to glide between pitches. The **Block** control switches between 2048 and 4096 samples for a tradeoff between latency and pitch-shifting quality.

## Build on Windows

Use CMake 3.22 or later and Visual Studio 2022 with the Desktop development with C++ workload.

```powershell
git clone --recurse-submodules https://github.com/NeutronStar714/pneutrino-extreme-shifter.git
cd pneutrino-extreme-shifter
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target PolyShifter_VST3 PolyShifter_Standalone
```

Version 1.0.0 builds are under `build/PolyShifter_artefacts/Release/`. CMake also declares an AU target for macOS. To reuse a matching JUCE checkout, pass `-DPNEUTRINO_JUCE_SOURCE_DIR="C:/path/to/JUCE"` at configure time.

JUCE 8 is a pinned submodule. The pitch shifter uses [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) 1.3.2 and headers from [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear/tree/0dd6b823783f1fe8768e2700e0937903f4270698). Their licenses are included in `libs/`.
