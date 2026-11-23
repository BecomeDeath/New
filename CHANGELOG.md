# Changelog

Each release's section becomes its GitHub release notes, and its first lines are what the
plug-in shows next to "An update is available". Add a section, set the same version in
CMakeLists.txt (`project (PromptMIDI VERSION ...)`), then push a tag `v<version>`.

## 1.0.0

- Installers for Windows, macOS and Linux: tick the formats you want (VST3, AU on macOS, the standalone app, the ReadMe) and choose where each one goes. Everything PromptMIDI needs comes with it.
- PromptMIDI now tells you when a new version is out ("An update is available", with a Download button). It can be turned off in Settings.
- The instrument sounds are installed once and shared by every format.
