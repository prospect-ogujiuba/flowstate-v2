Flowstate test build {{BUILD_ID}} (Windows)
============================================

Thanks for testing! Flowstate is an AI co-writer that lives on a MIDI track:
describe an idea, hear it in time with your song, shape it, and drag it in.
This is an early build, so rough edges are expected. Telling us about them
is the whole point.

You need 64-bit Windows 10 or 11, an internet connection, and a DAW that
loads VST3 plug-ins (Ableton Live, FL Studio, Bitwig, Reaper, Cubase, ...).


INSTALL (about a minute)
------------------------
1. Close your DAW.
2. Right-click the zip and choose "Extract All...", then open the extracted
   folder. (Running the installer from inside the zip doesn't work.)
3. Double-click install.cmd.
   - If Windows says "Windows protected your PC", click "More info", then
     "Run anyway". If it asks "Do you want to run this file?", click Run.
   - Then allow the admin prompt: the plug-ins go into the shared VST3 folder,
     C:\Program Files\Common Files\VST3.
   A window lists what was installed. Press Enter to close it.
4. Open your DAW and rescan plug-ins (in Ableton: Settings > Plug-Ins, turn
   on "Use VST3 Plug-In System Folders", then Rescan).

You get two plug-ins, made by "Flowstate":
- Flowstate: an instrument. Put it on a MIDI / instrument track. This is the
  one to use.
- Flowstate MIDI FX: the same thing as a MIDI effect, for DAWs that have a
  MIDI effect slot. Ableton can't open it; use the instrument there.
Standalone\Flowstate.exe in this folder runs Flowstate without a DAW.

The build isn't signed yet, which is why Windows warns about it.
To remove everything later: run install.cmd again with -Uninstall, from a
Command Prompt in this folder:  install.cmd -Uninstall

{{CHECKLIST}}

REPORTING
---------
Fill in FEEDBACK.txt (in this folder) and send it back, with screenshots if
you have them. If you get stuck before that, send what you have: the build
number ({{BUILD_ID}}), your DAW and Windows version, and what happened.
