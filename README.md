# chimera-core-opensamurai

[OpenSamurai](https://github.com/ToolAssisted-run/OpenSamurai), the
reconstruction of Sword of the Samurai (MicroProse, DOS, 1989), as a
[Chimera](https://github.com/ToolAssisted-run/chimera) **game core**
(`"kind": "game"`, see Chimera's `docs/game-cores.md`): the whole game - the
title, the career choices, character creation, the role-playing game, the
duels, the melees and the battles - stepped one video frame at a time in
miniBox's sandbox, packaged as `opensamurai.chimeraCore`.

**Built on upstream OpenSamurai, with two patches**: the first adds a weak hook
where the melee reads its tick counter, so the core can charge each read as
the machine OpenSamurai's melee was checked against; the second makes the
role-playing game's palette call hand its driver all eight words the driver
pushes, where six were whatever the host's registers held. Everything else is
OpenSamurai compiled from source, with Munt's libmt32emu for the Roland MT-32,
and the core's own stack switch, file layer and host in place of its SDL
frontend.

## What it is

- **Sword of the Samurai 445.03**, the release OpenSamurai is rebuilt from,
  from the user's own files - the original floppy's (the one the published
  speedrun plays) or the download sold on Steam and GOG.com; the Release
  setting says which. The two differ only in START.EXE (whose code
  OpenSamurai does not run: the game is the same) and the AdLib driver. The
  package carries none of the game's data: the files are the project's
  **firmware**, checked file by file against their SHA-1 at Init. A missing
  one is named; a damaged one is refused with both hashes; the other
  release's START.EXE is named, with the setting to choose; the floppy's
  AdLib driver, an older build than the one OpenSamurai's AdLib is rebuilt
  from, is refused by name.
- **A frame is one video frame** of the VGA (70.086 Hz): the game's programs
  wait for the retrace, and each reads its keys as it likes. The core's clock
  is OpenSamurai's own test clock, virtual: each look at it is 20 microseconds
  later and a wait jumps to its end, so a run is the same run everywhere. Two
  places cost otherwise, each measured: the melee counts its passes against
  its tick counter, and a read costs what a pass cost on the machine its
  reconstruction was checked against (10 microseconds in its start-up speed
  test, 0.8 milliseconds in its main loop - OpenSamurai's author, from the
  oracle's runs); and the battle's wait for its next step redraws its cursor
  on every look, so a look there costs 200 microseconds (seven times the
  speed; the battle's steps come with the frames, and memory and picture are
  equal at 20 and 200).
- **The controls are the PC's keyboard**, a button for each key the game reads:
  the eight directions of the numeric keypad, Enter and Space (the selector),
  Backspace (the second; a duel's parry), Esc, F1-F3 (the scrolls), the
  battle's keypad + - * and =, the digits (the battle's units), the letters
  (the samurai's name; R retreats) with Shift for capitals, and the commands
  Sound (Alt+V) and Graphics (Alt+Z). A button pressed is the key typed into
  the BIOS's buffer and its make code to the programs that read the keyboard
  themselves; held, it repeats as the AT keyboard's does after a reset (half a
  second, then 10.9 a second), on the virtual clock.
- **No campaign menu** (user-decided): a campaign can be neither saved,
  restored, abandoned nor quit, so Alt+S, Alt+R, Alt+N (which leads back to
  the career choices and their Restore) and Alt+Q are not buttons, and no
  other key reaches them (OpenSamurai's author checked every program's keys).
  Alt+J is the joystick, which the machine does not have.
- **The saved games**: the game keeps them in TALLTALE.DAT, which the disk
  brings blank (7,650 zeros) and without which Restore quits the game. A
  project starts with that blank file, as a new installation did, or with its
  own in the "savedgame" slot; the career choices' Restore Saved Game finds
  them. The file lives in guest memory, so a savestate carries it.
- **Settings** recorded in the project: the release, the sound device
  (AdLib, the default; the IBM PC speaker; Tandy's; the Roland MT-32; none),
  skipping the title (/NT), the random seed every program's random numbers
  are drawn from, and the date and time the PC's clock starts at.
- **The Roland MT-32** is Munt's libmt32emu, fed the MPU-401's bytes at their
  time and mixed with the game's sound in stereo. It needs RSOUND.SAM and an
  MT-32's two ROMs (v1.07, the first generation the game was made for; the
  DOSBox-X core's firmware ids and hashes), which the project brings as
  firmware when the sound is the Roland's.
- **Memory**: the DOS machine's 640 KB as the programs have it (every
  program's data segment at its original segment), the 1 KB block the
  launcher shares between the programs, the VGA's memory, the saved games,
  and a small Game State block (the frames shown, whether the game ended) -
  domains in place. **Properties** by name over them: the samurai's name, the
  video and sound modes, the duel's results.

## Building

```
git submodule update --init --recursive
make -C waterbox -f native.mk -j$(nproc)    # the native reference and the harnesses
make -C waterbox -f guest.mk -j$(nproc)     # core.wbx
./waterbox/build-package.sh                 # build/package/opensamurai.chimeraCore
```

miniBox is taken from `MB=`/`MINIBOX_DIR`, else `~/chimera/extern/chimera-common-minibox`;
it must be built with its C++ guest toolchain (`build/meson-cpp`,
`-Dguest_cpp=true`), since Munt is C++.

## Gate

```
./waterbox/run-gate.sh
```

Native == sandbox (picture, sound, every step's length, every memory domain)
over the title, a duel, a melee, a battle and the Roland's title;
determinism, a savestate before every step, a new host mid-run, turbo; the
pictures; each setting reaching the game; the keyboard (a key typed once,
Shift, repeat after half a second and not before); the saved games; the
commands; the property table, a poke and a freeze; the refusals; the
package - each new leg seen to fail on a break of its own. The MT-32's sound
is held to the sandbox, not to the native reference: Munt builds its tables in
floating point, and glibc's libm and musl's round differently.

The game is the user's: put Sword of the Samurai's files (the download's) in
`tests/roms-local`, the floppy's in `tests/roms-local/floppy` and the MT-32's
ROMs (MT32_CONTROL.ROM, MT32_PCM.ROM) in `tests/roms-local/roland`, or pass
`-d`. Without them only the build, the declarations and the no-files refusal
run.
