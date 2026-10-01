Flowstate test build {{BUILD_ID}} (macOS)
==========================================

Thanks for testing! This is an early build. It follows your DAW's tempo, meter
and transport, shows the first version of the interface, passes MIDI through
and saves with your project. It does NOT make ideas or sound yet, so a message
saying the agent service "isn't connected yet" is expected.


INSTALL (about a minute)
------------------------
1. Unzip this folder (double-click the zip) and leave it in Downloads.
2. Open Terminal (Applications > Utilities > Terminal).
3. Type  bash  followed by a space, drag install.sh from this folder onto the
   Terminal window, and press Return.
4. Open your DAW and rescan plug-ins. Logic does this on launch; in Ableton,
   turn on "Use Audio Units / VST3" in Settings > Plug-Ins and press Rescan.

You get two plug-ins, made by "Flowstate":
- Flowstate: an instrument. Put it on a MIDI / software instrument track.
- Flowstate MIDI FX: a MIDI effect (in Logic, the MIDI FX slot).
There's also a Standalone app in your Applications folder (in your home folder).

To remove everything later: the same as step 3, but type  --uninstall  after
the file name before pressing Return.

The build isn't signed by Apple yet, which is why it installs from Terminal.
The installer removes macOS's download block from the Flowstate files only.


WHAT TO CHECK
-------------
1. Loads: Flowstate on a MIDI track opens a dark window with a strip at the
   top, an "Ideas" panel on the right and a prompt box at the bottom.
2. Follows the DAW: the strip shows your tempo and meter. Change the tempo
   (say to 97) and the meter (say to 7/8): it follows within a second.
3. Transport: press play. It counts bars and beats in time; stop says
   "stopped"; with a loop on, the count wraps with the loop.
4. Space bar: click an empty part of the window, press Space. Your DAW
   starts and stops as usual.
5. Typing: click the prompt box and type words with spaces. Nothing should
   trigger DAW shortcuts. Press Esc, then Space: the DAW transport toggles.
6. Resize: drag the window corner, close and reopen it. Same size.
7. Saving: save the project, close it, reopen it. No errors, same size.
8. MIDI FX (Logic): put Flowstate MIDI FX in an instrument track's MIDI FX
   slot and play. The instrument still sounds. (Ableton can't open MIDI
   effect plug-ins, so skip this one there.)
9. Two at once: two Flowstate windows open together both work.
10. Design gallery (the most important one for us right now):
   a. Quit Logic. In Terminal, paste this line and press Return:
        FLOWSTATE_UI_PAGE=gallery.html "/Applications/Logic Pro.app/Contents/MacOS/Logic Pro"
      (If your Logic is called "Logic Pro X", use that name in both places.)
   b. Open Flowstate on a track. Instead of the normal window you see a
      page of buttons, knobs and switches.
   c. Press Tab repeatedly: it moves through the controls with a cyan
      outline. If Tab only reaches text boxes, turn on System Settings >
      Keyboard > Keyboard navigation and try again.
   d. Knobs turn with the arrow keys and by dragging.
   e. The settings panel opens, and Esc closes it.
   f. With a button outlined, press Space: Logic starts or stops, and the
      button is NOT pressed.
   g. Quit Logic. Opening it normally brings back the normal window.


REPORTING
---------
For each problem (or "all fine"), send:
- the build: {{BUILD_ID}}
- your DAW and its version, and your macOS version
- which check, what you did, and what happened (a screenshot helps)
