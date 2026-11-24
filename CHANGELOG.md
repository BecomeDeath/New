# Changelog

Each release's section becomes its GitHub release notes, and its first lines are what the
plug-in shows next to "An update is available". Add a section, set the same version in
CMakeLists.txt (`project (PromptMIDI VERSION ...)`), then push a tag `v<version>`.

## 1.2.0

- Updates install from inside PromptMIDI, like a game launcher: no browser, no installer to run. It downloads, verifies and installs the new version, keeping the parts and folders you chose.
- "An update is available" now appears in the middle of the window, with Update now and Later.

## 1.1.0

- PromptMIDI now thinks before it answers. It reads your prompt into several plans, writes hundreds of drafts, develops the best and keeps the one that meets everything you asked for and is judged best: better melodies, cleaner harmony and endings. A panel shows each stage with a live timer, the best draft so far, Use best so far and Cancel.
- Settings > Thinking: Quick (instant, as before), Thorough (the default) or Deep.
- Hover over the verdict under the title to see how a take was chosen.

## 1.0.0

- Installers for Windows, macOS and Linux: tick the formats you want (VST3, AU on macOS, the standalone app, the ReadMe) and choose where each one goes. Everything PromptMIDI needs comes with it.
- PromptMIDI now tells you when a new version is out ("An update is available", with a Download button). It can be turned off in Settings.
- The instrument sounds are installed once and shared by every format.
