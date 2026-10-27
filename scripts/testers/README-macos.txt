Flowstate test build {{BUILD_ID}} (macOS)
==========================================

Thanks for testing! Flowstate is an AI co-writer that lives on a MIDI track:
describe an idea, hear it in time with your song, shape it, and drag it in.
This is an early build, so rough edges are expected. Telling us about them
is the whole point.

You need macOS 11 or later, an internet connection, and a DAW that loads
Audio Units or VST3 (Logic Pro, Ableton Live, Bitwig, ...).


INSTALL (about a minute)
------------------------
1. Double-click the zip to unzip it, and leave the folder in Downloads.
2. Open Terminal (Applications > Utilities > Terminal).
3. Type  bash  followed by a space, drag install.sh from this folder onto the
   Terminal window, and press Return.
4. Open your DAW and rescan plug-ins. Logic does this on launch; in Ableton,
   turn on "Use Audio Units" or "Use VST3" in Settings > Plug-Ins and press
   Rescan.

You get two plug-ins, made by "Flowstate":
- Flowstate: an instrument. Put it on a MIDI / software instrument track.
  This is the one to use.
- Flowstate MIDI FX: the same thing as a MIDI effect (in Logic, the MIDI FX
  slot). Ableton can't open MIDI effects, so use the instrument there.
There's also a Flowstate app in the Applications folder in your home folder,
which runs without a DAW.

The build isn't signed by Apple yet, which is why it installs from Terminal
instead of by double-clicking. The installer removes macOS's download block
from the Flowstate files only. If macOS still says Flowstate "can't be
opened", run the installer again and tell us.

To remove everything later: the same as step 3, but type  --uninstall  after
the file name before pressing Return.

{{CHECKLIST}}

REPORTING
---------
Fill in FEEDBACK.txt (in this folder) and send it back, with screenshots if
you have them. If you get stuck before that, send what you have: the build
number ({{BUILD_ID}}), your DAW and macOS version, and what happened.
