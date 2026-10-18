# Install on Bazzite (draft, not tested yet)

## What was checked

- Bazzite docs recommend, in order: Bazzite Portal, Bazaar (Flatpak), Homebrew, containers, AppImage. They advise avoiding rpm-ostree layering.
- Flathub Bitwig grants full host filesystem access and loads plugins from the Flathub LinuxAudio plugin extensions. Plugins in system paths were reported not to load in the Flatpak (Fedora forum), and one user also had trouble with home-folder plugins. Needs a real test.
- ROCm officially supports Ubuntu and RHEL, so the engine goes in an Ubuntu container.

## Plan

1. Engine: Ubuntu Distrobox with ROCm PyTorch (steps to be written after testing).
2. Plugin: ship as a Flathub LinuxAudio plugin extension; also provide a plain `.vst3`/`.clap` for the home folder.
3. Test both in Bitwig Flatpak on the real machine.
