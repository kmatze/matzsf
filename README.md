matzsf ...
==========

- ... version 1.006 by ma.ke. 2018-03-15 is a tcl package for windows
- ... is based on TinySoundFont v0.8 synthesizer by Bernhard Schelling (https://github.com/schellingb/TinySoundFont)
- ... (should be on other system where tsf works)
- ... is a hobby project with no warrenty
- ... plays midifile in tcltk

**features:**

- load soundfonts and midi files
- play, pause (toggle pause and continue), stop
- get current status and playing informations
- extra: mciSendString function to use external midi device or other things ;-) (on non windows system you must comment these function)

**compile:**

- to compile matzsf you need gcc and the header files of tcl/tk 8.3.x or higher.
- i use gcc 4.8.3 32bit and tclkit 8.40 under Windows 10.
- you can use the simple batch file compile_sf.bat and change it for you.

**general working steps in wish (tk shell):**

	1. load matzsf.dll
	2. load soundfont
	3. load midifile
	4. play

**commands for working steps:**

	matzsf::loadSF2 file   -> get list of presets
	matzsf::loadMID file   -> get list (#chn, #prgs, #nts, first_msec, length_msec
	matzsf::play           -> play loaded midifile
	matzsf::pause          -> toogle command to pause and continue
	matzsf::stop
	matzsf::mci "mciSendString"

**commands for informations:**

	matzsf::help
	matzsf::status         -> get list (player, soundfont, #presets, midifile)
	matzsf::playinfo       -> get list (playtime_msec, chn, key, vel, dur)

**example matzsf.tcl**

	start wish
	source matzsf.tcl      -> load GB.sfs, load venture.mid, play
   
![tclgif](/matze.gif)

**todo:**

- ???

TRY IT ;-)

I hope you have fun.

Greetings - kmatze (aka ma.ke.) - 15.03.2018





