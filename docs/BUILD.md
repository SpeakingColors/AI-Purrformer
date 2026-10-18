# Build and install (Bazzite, one step at a time)

The plugin is built inside the same Ubuntu distrobox as the engine, then the `.vst3` is copied to `~/.vst3`.

1. Get the code (inside the distrobox): `git clone https://github.com/SpeakingColors/AI-Purrformer && cd AI-Purrformer`
2. Build the plugin: `./scripts/build-plugin.sh` (installs the build tools with apt, builds, copies `AI Purrformer.vst3` to `~/.vst3`)
3. Start the engine in a second terminal (also inside the distrobox): `./scripts/run-engine.sh`
4. Open your DAW, rescan plugins, load **AI Purrformer** as an instrument.
5. Press **Capture from host**, press play, loop your MIDI clip once, press **Render**, then play.

Checks that were run in the cloud container (Ubuntu 24.04, JUCE 8.0.4): build of VST3 and Standalone, and `purr_selftest` (capture, engine round trip, host-synced playback, state save and load): all pass. The window was also opened under a virtual display and looked right.

Not verified yet, needs your machine:
- Bitwig Flatpak finding `~/.vst3`, and whether the plugin's libraries (libcurl, freetype, ALSA) resolve inside the Flatpak runtime. If Bitwig does not list it, say so and we switch to static linking or the Flatpak extension.
- Studio One loading the VST3.
- Real voices: the engine only has the built-in test voices until the ROCm test and the SoulX-Singer adapter are done.
