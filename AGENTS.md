# AGENTS.md - OpenSamurai core for Chimera

This repository builds OpenSamurai, the reconstruction of Sword of the
Samurai (MicroProse, DOS, 1989), as a game core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns. It is a game core: one game rebuilt as a core and stepped one
video frame at a time, not an emulator. It produces one file,
`opensamurai.chimeraCore`, which Chimera's sandbox (miniBox) runs on Linux
and on Windows. The package carries none of the game's data and no MT-32 ROM.

## Layout

- `extern/OpenSamurai` - submodule: the game (upstream OpenSamurai), built
  unpatched; there is no `patches/` directory. Munt, the Roland MT-32, is
  its own submodule at `extern/OpenSamurai/extern/munt`.
- `waterbox/samurai-driver.c`, `game-state.c`, `coro.c`, `files.c`, `sha1.c`,
  `wbx-entry.c` - the core itself.
- `waterbox/munt-config.h` - Munt's `config.h`, the core's own.
- `waterbox/gen-config.py` - writes `waterbox/waterbox.config`.
- `waterbox/waterbox.config` - what Chimera is told: kind, settings, buttons,
  the firmware list. Generated.
- `waterbox/file_slots.json`, `default_keybinds.json`,
  `package-licenses.json` - packed as they are.
- `waterbox/sources.mk`, `native.mk`, `guest.mk` - the build.
- `waterbox/apply-patches.sh`, `build-package.sh`, `run-gate.sh` - scripts.
- `waterbox/run-native.c`, `run-wbx.c`, `gate-harness.h` - the gate's two
  harnesses.
- `waterbox/tests/` - the gate's checkers. `make-project.py` is a helper for
  a frontend run; no script here runs it.
- `tests/roms-local/` - the game's files for the gate; `build/` - everything
  built. Both gitignored.
- `docs/BUILDING.md` - the build in detail.
- `.github/workflows/chimera.yml` - CI: the authoritative build recipe.

## Set up the build environment

On Ubuntu, as CI does:

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev
```

The sources, each with its submodules, recursively: Munt is nested
(`<chimera>` is the Chimera checkout):

```
git submodule update --init --recursive
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

miniBox, the sandbox host and the guest toolchain, is Chimera's submodule
`extern/chimera-common-minibox`. This core needs two builds of it, because
Munt is C++. Build them once:

```
export MINIBOX_DIR=<chimera>/extern/chimera-common-minibox
[ -f "$MINIBOX_DIR/build/meson-linux/build.ninja" ] || meson setup "$MINIBOX_DIR/build/meson-linux" "$MINIBOX_DIR"
meson compile -C "$MINIBOX_DIR/build/meson-linux"
[ -f "$MINIBOX_DIR/build/meson-cpp/build.ninja" ] || meson setup "$MINIBOX_DIR/build/meson-cpp" "$MINIBOX_DIR" -Dguest_cpp=true
meson compile -C "$MINIBOX_DIR/build/meson-cpp"
```

The `meson-cpp` build fetches the GCC source matching the host's gcc (about
84 MB, with `curl`) to build libstdc++ for the guest. It needs the network.
Without `MINIBOX_DIR` (or `-m <dir>`, or `MB=<dir>` for make) the scripts look
in `~/chimera/extern/chimera-common-minibox`.

## Build

```
make -C waterbox -f native.mk -j"$(nproc)"    # build/native/run-native, run-wbx
make -C waterbox -f guest.mk -j"$(nproc)"     # build/guest/core.wbx
./waterbox/build-package.sh -r <chimera>      # <chimera>/build/Cores/opensamurai.chimeraCore
```

- `build-package.sh` runs `guest.mk` again, so the first two lines can be
  replaced by `./waterbox/run-gate.sh`, which runs both. Do not run
  `build-package.sh` first in a fresh clone: it writes a log into `build/`
  before that directory exists.
- `build-package.sh -o <dir>` writes to `<dir>` instead; with neither option
  the package is `build/package/opensamurai.chimeraCore`.
- A hand-built package is stamped `<commit>+local` (`-dirty` when tracked
  files differ from HEAD). It is for testing. CI stamps the commit.

## Install the core into Chimera

Chimera includes no cores and downloads nothing. A core is a file in its
`Cores` folder: `<chimera>/build/Cores/` in a source checkout (where
`build-package.sh -r <chimera>` writes), the `Cores` folder beside
`Chimera.exe` in a release bundle, or the folder chosen in
File > Core Manager > Change folder... File > Core Manager lists the folder;
Refresh List rescans it.

## Test before you commit

```
./waterbox/run-gate.sh
```

- It must end with `0 failed`; it exits non-zero otherwise.
- The game legs need the user's files in `tests/roms-local` (the download's
  files, and a `TALLTALE.DAT` with saved games), `tests/roms-local/floppy`
  (the floppy's `START.EXE`, `ASOUND.SAM` and blank `TALLTALE.DAT`) and
  `tests/roms-local/roland` (the MT-32's two ROMs). Without the game the gate
  runs only the build, the stack check, the declarations and the no-files
  refusal. That is all CI can run. A change to the game's behaviour is not
  tested until the gate has run with the files.
- `run-gate.sh -q` skips the build; the `build:fresh` leg fails if a source
  is newer than `core.wbx`.
- The MT-32's sound is held to the sandbox only, not to the native
  reference.
- Results and pictures: `build/gate/`.

CI also runs Chimera's contract tests against the package
(`docs/BUILDING.md`, "Chimera's contract tests").

## Rules of this repository

- `extern/OpenSamurai` is a submodule and is built unpatched. A change to
  the game goes upstream and arrives here by moving the pin. Never commit
  inside the submodule. `waterbox/apply-patches.sh` would apply a numbered
  series from `patches/` if one were added; today it finds none.
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs: the core's clock is
  virtual. A savestate must round-trip. The gate checks both; a change that
  breaks either is a bug.
- `waterbox/waterbox.config` is generated. Change the driver or
  `gen-config.py`, then run `python3 waterbox/gen-config.py`. The gate's
  `wire:config==driver` leg fails when they disagree.
- The game's stack comes from `mmap` with `MAP_STACK` (`coro.c`). The gate's
  `stacks:map-stack` leg holds this; Windows needs it.
- No campaign menu: Save Game, Restore Game, New Game and Quit are not
  buttons. The gate's `buttons:no-campaign-menu` leg holds this.
- Run the gate before committing. A new leg needs a negative control: break
  the thing it checks, see the leg fail, revert.
- Never commit the game's files or the MT-32's ROMs. Never add network
  access.
- Shell scripts stay executable (git mode 100755). Docs are plain ASCII.
- Commit subjects say what is now true, most with a prefix: `feat:`,
  `fix:`, `build:`, `ci:`. A pin move names the upstream commit:
  `feat: OpenSamurai <commit> - <what it brings>`. The body says in prose
  what changed and what was measured.
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - requirements, every script option, the files the core
  needs, troubleshooting.
- `README.md` - what the core is and how the game behaves in it.
- In the Chimera checkout: `docs/game-cores.md`, `docs/porting-a-core.md`,
  `docs/core-manager.md`, `docs/gates.md`.
