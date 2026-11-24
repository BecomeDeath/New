PromptMIDI @VERSION@
====================

Describe the music in words, get multi-track MIDI.

Type a request such as "Generate me a fantasy orchestral midi" or "Make me a soft piano
midi", press Generate, listen with Play, then drag the result into your DAW.

Website:  https://becomedeath.github.io/TLComposer/promptmidi.html
Source:   https://github.com/BecomeDeath/PromptMIDI


WHAT WAS INSTALLED
------------------

You chose the parts in the installer. These are the usual places:

                 Windows                                   macOS
  VST3           C:\Program Files\Common Files\VST3        /Library/Audio/Plug-Ins/VST3
  VST2           C:\Program Files\Steinberg\VSTPlugins     /Library/Audio/Plug-Ins/VST
  AU             -                                         /Library/Audio/Plug-Ins/Components
  App            C:\Program Files\PromptMIDI               /Applications
  Sounds         C:\ProgramData\PromptMIDI\Sounds          /Library/Application Support/PromptMIDI/Sounds
  This ReadMe    C:\Program Files\PromptMIDI               /Library/Application Support/PromptMIDI

  Linux (install.sh): VST3 in ~/.vst3, VST2 in ~/.vst, the app in ~/.local/bin and the
  sounds in ~/.local/share/PromptMIDI (or /usr/local/... when run with sudo).

"Sounds" are the instruments the preview plays (the GeneralUser GS SoundFont). Every
format shares the one copy. Nothing else is needed: no runtimes, no other downloads.


FIRST STEPS
-----------

1. Open your DAW and rescan plug-ins if it doesn't do so by itself.
2. Add PromptMIDI to an instrument track. It is listed as an instrument
   (category Instrument | Generator).
3. Type what you want, press Generate, press Play to hear it.
4. Drag "Drag MIDI into your DAW" onto your timeline. Most DAWs split it into one
   track per instrument. Drag a row of the track list to take a single part.

Or open the PromptMIDI app on its own, without a DAW, and export .mid files.

PromptMIDI only makes sound while you press Play inside it. It is silent while your DAW
plays, so after dragging the MIDI out you only hear your own instruments.


THINKING
--------

When you press Generate, PromptMIDI thinks before it answers: it reads your prompt into
several plans, writes hundreds of drafts, develops the most promising and keeps the one that
meets everything you asked for and is judged best (melody, harmony, balance, mood, shape).
A panel shows each stage with a timer; "Use best so far" stops early, Cancel keeps the take
you had. Hover over the verdict under the title to see how a take was chosen.

Settings > Thinking: Quick (instant), Thorough (the default, about a second) or Deep (a few
seconds, longer for long orchestral cues).


UPDATES
-------

When a new version is out, PromptMIDI asks in the middle of its window: Update now or
Later. Update now downloads the new version, checks it, and installs it over the old one,
keeping the parts and folders you chose (your settings and presets stay). The system
asks once for permission. A DAW keeps the loaded plug-in until it closes: on macOS and
Linux the new version is there the next time you open the DAW; on Windows it installs as
soon as you close the DAW. The standalone app restarts into the new version.

PromptMIDI checks at most twice a day by downloading a small public file from GitHub;
nothing about you or your music is sent. Turn it off in Settings ("Tell me when an
update is available").


CLAUDE AI MODE (OPTIONAL)
-------------------------

The offline engine needs nothing. For Claude AI mode, click Claude AI, open Settings
and paste an Anthropic API key. The key stays on this computer and is only sent to
api.anthropic.com.


UNINSTALLING
------------

Windows  Settings > Apps > PromptMIDI > Uninstall
macOS    Delete the files listed above (PromptMIDI.vst3, PromptMIDI.component,
         PromptMIDI.vst, PromptMIDI.app and the PromptMIDI folder in
         /Library/Application Support).
Linux    Run uninstall.sh in ~/.local/share/PromptMIDI (or /usr/local/share/PromptMIDI).

Your settings stay behind in %APPDATA%\PromptMIDI (Windows),
~/Library/Application Support/PromptMIDI (macOS) or ~/.config/PromptMIDI (Linux).
Delete that folder too to remove everything, including a saved API key.


TROUBLESHOOTING
---------------

- The DAW doesn't list PromptMIDI: rescan, and check the DAW scans the folder the
  plug-in went to (see the table above).
- macOS says the plug-in "can't be opened" or is damaged: run in Terminal
    xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/PromptMIDI.vst3
  (and the same for PromptMIDI.component and /Applications/PromptMIDI.app).
- The preview plays a simple synth instead of real instruments: the sounds are
  missing. Run the installer again (Sounds are always installed).
- Linux: if the app doesn't start, install the libraries install.sh lists
  (libcurl, ALSA, FreeType, Fontconfig).


CREDITS AND LICENCES
--------------------

The Licences folder next to this file has the full texts.

- GeneralUser GS SoundFont by S. Christian Collins.
- TinySoundFont by Bernhard Schelling (MIT).
- Inter typeface by The Inter Project Authors (SIL Open Font License 1.1).
- Built with JUCE (AGPLv3 / commercial).
- Melody, rhythm and harmony statistics learned from the MidiCaps dataset
  (CC BY-SA 4.0), which annotates the Lakh MIDI Dataset (CC BY 4.0).

VST is a trademark of Steinberg Media Technologies GmbH. Audio Units is a trademark of
Apple Inc.
