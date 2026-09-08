NECTAR — PIKMIN FOR WINDOWS (x86-64)

A native Windows build. No emulator, no Wine.

This build runs the game's original JAudio sound engine: music, sound
effects and cinematic audio all play.


WHAT YOU NEED
-------------
  - 64-bit Windows.
  - GPU drivers with OpenGL 3.3 or newer. Intel, NVIDIA and AMD drivers all
    have it. Windows' generic "Basic Display Adapter" driver does NOT, and
    the game will not start on it.
  - Your legal, uncompressed copy of Pikmin USA Rev. 1 (GPIE01), as ISO or GCM.

The ROM and any Nintendo proprietary resources are not included. Your image
is neither copied nor modified during installation.


INSTALLING
----------
1. Extract this folder anywhere you like. Nothing is installed system-wide.
2. Run nectar-launcher.exe (double-click).
3. It asks for the ISO/GCM image and a destination folder.
4. When it finishes, the game starts on its own.

To play later, go to the installation folder and run nectar-launcher.exe
again: it sees the game is already installed and launches it directly.

You can also install without dialogs, from cmd or PowerShell:

  nectar-launcher.exe --rom C:\path\to\pikmin.iso --install-dir C:\Games\Nectar

Options: --extract-only (install without launching), --skip-verify, --help.

The installer checks the image against a known-good dump of Pikmin USA
Rev. 1 before extracting, and re-reads each file it writes. That catches
copies damaged in transfer and failing drives, which are the usual reason a
game installs fine and then fails strangely. It adds about a minute;
--skip-verify skips it.


THE CONSOLE WINDOW IS DELIBERATE
--------------------------------
The game opens a console window with diagnostic messages. It is not a bug.
SDL2 would normally hide it, and with it every message the port prints. It
is kept so that a failed launch says why instead of vanishing silently.


IF SOMETHING GOES WRONG
-----------------------
The most useful thing you can send is the text from that console. To keep
it, run the game from cmd with the output redirected:

  cd C:\Games\Nectar
  nectar.exe > log.txt 2>&1

Common failures and what they mean:

  Closes instantly, no window
      SDL2.dll is missing from beside the .exe, or Windows blocked it. Check
      that SDL2.dll sits in the same folder as nectar.exe.

  "Could not initialize window/OpenGL"
      Your GPU drivers do not offer OpenGL 3.3. Update them from the
      manufacturer's website, not from Windows Update.

  Starts but cannot find the game data
      The game looks for the assets\ folder in the directory it is run from.
      Launch it from its installation folder, not by absolute path from
      somewhere else.

  The installer rejects the image
      It must be Pikmin USA Rev. 1 (GPIE01, revision 1), uncompressed.
      Convert RVZ/WIA/GCZ to ISO with dolphin-tool.

  No sound, but the game runs
      The log says "no audio device yet; playing silently and retrying". The
      default playback device was busy or unavailable at launch. The game
      keeps trying, so sound starts on its own once a device is free.


IN-GAME SETTINGS — F1
---------------------
Press F1 at any time for resolution, display mode, render scale, VSync,
frame rate and controls.

Under Mods:
  - Mouse wheel: either picks which Pikmin colour to throw next, or zooms
    the camera.
  - Pikmin limit: how many Pikmin may be on the field at once, 50 to 999.
    The original is 100. It applies when a stage loads. High values do cost
    frame rate, and how much depends on your machine.
  - Day length: 5 to 30 minutes per in-game day, 10 being the original. It
    stretches the game's own clock, so nothing moves faster or slower --
    sunset simply arrives sooner or later.
  - Chain Pikmin actions: Pikmin look for more work after finishing a task.
    Off by default, to stay faithful to the original.


KNOWN STATE
-----------
  - Sound: the original JAudio engine. Music, effects and cinematic audio.
  - Cinematic video: not implemented. The audio plays, the picture does not.
  - 30, 60 and 120 FPS selectable from the F1 menu.
