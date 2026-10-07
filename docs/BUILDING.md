# Building the OpenSamurai core

This repository builds OpenSamurai, the reconstruction of Sword of the
Samurai (MicroProse, DOS, 1989), as a Chimera game core: one game rebuilt as
a core, not an emulator. The result is one file, `opensamurai.chimeraCore`,
which Chimera loads. The steps below are the ones the repository's CI runs
from a fresh clone (`.github/workflows/chimera.yml`).

Placeholders used in this document:

- `<core>`: the checkout of this repository.
- `<chimera>`: a checkout of Chimera
  (https://github.com/ToolAssisted-run/chimera).
- `<minibox>`: `<chimera>/extern/chimera-common-minibox`, the miniBox
  submodule of that checkout. miniBox is the sandbox host and the guest
  toolchain.

## Requirements

- Linux, x86_64. CI builds on GitHub's `ubuntu-latest` runner. Cores are built
  on Linux; the package that comes out runs on Linux and on Windows.
- git, and the packages CI installs:

  ```
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev
  ```

- The compilers are the distribution's gcc and g++ (`build-essential`). The
  workflow pins no compiler version.
- `curl`. miniBox's build of the C++ guest toolchain calls it (below). The
  workflow's package list does not name it.
- The .NET SDK 8.0. CI sets it up with `actions/setup-dotnet@v4`,
  `dotnet-version: '8.0'`. It builds Chimera and runs Chimera's contract
  tests; the core's own build does not use it. Chimera's README installs
  Microsoft's SDK with
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`
  and says a distribution-built SDK lacks targets the frontend needs.

What gets downloaded or built along the way:

- Nothing in this repository's scripts downloads anything. OpenSamurai and
  Munt (the Roland MT-32, C++) are compiled from the submodules.
- miniBox builds the guest C library (musl) from sources in its own checkout.
- miniBox's C++ guest toolchain is a libstdc++ built for the guest. Its build
  fetches the GCC source that matches the host's gcc (about 84 MB) and
  compiles libstdc++ from it. This step needs the network once.

## Get the sources

CI checks out both repositories with `actions/checkout@v6` and
`submodules: recursive`, Chimera at its `main` branch. By hand:

```
git clone --recursive https://github.com/ToolAssisted-run/chimera-core-opensamurai.git <core>
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

In a clone made without `--recursive`:

```
git submodule update --init --recursive
```

This repository has one submodule, `extern/OpenSamurai` (the game). Munt is a
submodule of OpenSamurai itself, at `extern/OpenSamurai/extern/munt`, so the
checkout must be recursive.

The Chimera checkout can be anywhere. CI puts it in `chimera-checkout` inside
the core checkout. The scripts find miniBox in this order:

1. `-m <miniBox dir>` on the script's command line (`MB=<dir>` for the
   makefiles).
2. The `MINIBOX_DIR` environment variable. CI sets it.
3. `<chimera root>/extern/chimera-common-minibox`, for
   `build-package.sh -r <chimera root>` only.
4. `~/chimera/extern/chimera-common-minibox`.

The rest of this document sets the variable once:

```
export MINIBOX_DIR=<chimera>/extern/chimera-common-minibox
```

## Build miniBox

This core needs two build directories of miniBox, because Munt is C++. The
workflow's commands:

```
mb=<chimera>/extern/chimera-common-minibox
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

- `build/meson-linux` has `source/host/libminiboxhost.so`, the sandbox host
  library that the `run-wbx` harness links.
- `build/meson-cpp` has the C++ guest toolchain: `guest-sysroot` with musl
  and libstdc++, which `waterbox/guest.mk` compiles and links against.

`guest.mk` compiles with the system's `gcc` and `g++` through the sysroot's
specs file, and looks for libstdc++'s headers under the version that
`gcc -dumpfullversion` prints. The gcc on the PATH must be the one the C++
guest toolchain was built with.

CI keeps both directories between runs with `actions/cache@v4`. By hand they
simply stay, and the `[ -f ... ] ||` lines skip `meson setup` when a
directory is already there.

## Build the core

### Patches

There are none. OpenSamurai is built as upstream has it, and this repository
has no `patches/` directory. `waterbox/apply-patches.sh` is still there and
both makefiles still run it (`waterbox/sources.mk`, the `build/patches.stamp`
rule). With no patches it only checks that `extern/OpenSamurai` is checked
out, prints `no patches to apply` and succeeds.

If a `patches/` directory with numbered patches is ever added, the script
applies them to `extern/OpenSamurai` and judges the series as a whole: a
pristine tree gets every patch, a tree that is exactly what the series leaves
behind is left alone, and anything in between is an error that names the
files.

### The native reference and the guest

```
cd <core>
make -C waterbox -f native.mk -j"$(nproc)"
make -C waterbox -f guest.mk -j"$(nproc)"
```

- `native.mk` builds the native reference with the host's gcc and g++: the
  same OpenSamurai, Munt and core sources as the guest. It makes
  `build/native/run-native`, which drives the core's exports directly, and
  `build/native/run-wbx`, which drives `core.wbx` through the miniBox host as
  the frontend does. The gate compares the two.
- `guest.mk` builds `build/guest/core.wbx` with the C++ guest toolchain. The
  file counts as built only after miniBox's `check-wbx.sh` passes it (no
  thread-local storage, no `%fs`, no red zone).

Both makefiles take `MB=<miniBox dir>`; without it they use `MINIBOX_DIR`.
CI does not call them directly: `waterbox/run-gate.sh` runs both, and
`waterbox/build-package.sh` runs `guest.mk` again.

## Build the package

```
./waterbox/build-package.sh -r <chimera>
```

Usage: `./waterbox/build-package.sh [-m <miniBox dir>] [-r <chimera root>] [-o <out dir>]`

- `-r <chimera root>` writes
  `<chimera root>/build/Cores/opensamurai.chimeraCore` and removes any
  `<chimera root>/build/CoreCache/opensamurai-*` directory. This is what CI
  runs.
- `-o <out dir>` writes `<out dir>/opensamurai.chimeraCore` instead.
- With neither, the file is `build/package/opensamurai.chimeraCore`.
- `-m <miniBox dir>` names miniBox and overrides `MINIBOX_DIR`.

The script runs `guest.mk` (log: `build/package-make.log`), checks
`core.wbx`, and packs it with `waterbox/waterbox.config`,
`waterbox/default_keybinds.json`, `waterbox/file_slots.json`, the licences
(from `waterbox/package-licenses.json`) and a `build.json` that records what
built it. It writes the archive twice and stops if the two differ. On success
it prints `package sha1 <hash>` and `packaged -> <path>`.

The package's version is the commit it was built from:

- CI passes `CORE_VERSION` (the commit). That value is stamped as it is.
- Without `CORE_VERSION` the script stamps `<commit>+local`, and
  `<commit>-dirty+local` when tracked files differ from HEAD.
- The commit's date, in UTC, is stamped beside it as `versionDate`.

A package built by hand is for testing. Chimera's publish script refuses a
version that carries `+local` or `-dirty`.

The script builds the guest itself, so it also works on a fresh clone with
nothing built yet; its build log is `build/package-make.log`.

## Install it into Chimera

Chimera includes no cores and downloads nothing: it has no network code. A
core gets into Chimera as a file somebody puts in its `Cores` folder.

- In a Chimera source checkout the folder is `<chimera>/build/Cores/`.
  `./waterbox/build-package.sh -r <chimera>` writes the package straight
  there.
- In a release bundle the folder is `Cores`, beside `Chimera.exe`. Copy
  `opensamurai.chimeraCore` into it. Another folder can be chosen in
  File > Core Manager > Change folder...
- File > Core Manager lists what is in the folder. Refresh List rescans it
  for a package copied in while the window is open.

The same package file works on Linux and on Windows. Chimera's sandbox,
miniBox, runs the guest inside it on either.

Without building: this repository's CI publishes the package on its Releases
page (https://github.com/ToolAssisted-run/chimera-core-opensamurai/releases).
A rolling `dev` release follows every green push to `main`. A dated
`nightly-YYYY-MM-DD` release comes from the scheduled run (04:00 UTC), only
when `main` moved since the last one. A published package is named
`opensamurai-<version>.chimeraCore`.

In Chimera a new project picks the core in File > New Project... (Kind:
Game).

## Run the gates

### The core gate

```
./waterbox/run-gate.sh
```

Usage: `./waterbox/run-gate.sh [-q] [-m <miniBox dir>] [-d <game dir>]`

- `-q` skips the build and tests what is built. A `core.wbx` older than a
  source then fails the `build:fresh` leg.
- `-d` names the folder that holds the game's files (default
  `tests/roms-local`; `SAMURAI_DIR` also sets it).

It needs miniBox built, both directories. It builds the native reference and
the guest, then proves that the sandboxed core plays exactly as the native
reference does (picture, sound, every step's length, the clock, every memory
domain) over the title, a duel, a melee, a battle and the Roland's title;
that a second sandboxed run is the same; that a savestate before every step
loses nothing; and that a new host can finish a run from a state taken half
way. It then checks the pictures, each setting reaching the game, the
keyboard, the saved games, the commands, the property table, a poke and a
freeze, the refusals, that the game's stack is asked for as a stack
(`MAP_STACK`), that the campaign menu's commands are no buttons, and that the
package is the same twice.

The MT-32's sound is held to the sandbox, not to the native reference. Munt
builds its tables in floating point, and glibc's libm and musl's round
differently.

It prints one line per leg (PASS, FAIL or SKIP) and ends with
`<n> ok, <n> failed, <n> skipped`. The exit status is 0 only when nothing
failed. Its work files and pictures are in `build/gate/`.

The game's files are the user's and are never in the repository. The gate
takes them from `tests/roms-local`, which is gitignored:

- `tests/roms-local/`: Sword of the Samurai's files, the download's. A
  `TALLTALE.DAT` with saved games here is what the saved-games legs restore
  from.
- `tests/roms-local/floppy/`: the floppy's files: its `START.EXE`, its
  `ASOUND.SAM` and its blank `TALLTALE.DAT`.
- `tests/roms-local/roland/`: the MT-32's ROMs, `MT32_CONTROL.ROM` and
  `MT32_PCM.ROM`.

What is skipped without them:

- Without `RP.EXE` in the game folder, or without `floppy/START.EXE`, only
  the build, the stack check (`stacks:map-stack`), the declarations
  (`wire:config==driver`, `buttons:no-campaign-menu`) and the refusal of a
  project with no files run. Everything else is one `game SKIP` line, the
  package leg included. This is what CI runs: a public runner does not have
  the game.
- Without `floppy/ASOUND.SAM` the `refuse:floppy-adlib` leg is skipped.
- Without the download's `START.EXE` the `firmware:other-release` leg is
  skipped.
- Without a `TALLTALE.DAT` in the game folder the `command:sound` leg is
  skipped.

The script has no SKIP for the Roland's legs or for the `saved-games` leg.
Once the game's files are there, those legs need the MT-32's ROMs and the two
`TALLTALE.DAT` files.

This repository has no frontend gate script.

### Chimera's contract tests

CI runs Chimera's own tests against the package it just built. They need
Chimera built first, with the workflow's commands:

```
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false
```

CI runs these before it builds miniBox. The core's own build reads only
miniBox's build directories, so the order between the two does not matter.

Then, with the package in `<chimera>/build/Cores`:

```
cd <chimera>
CHIMERA_CORES_DIR="$PWD/build/Cores" dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

They prove the package is readable, is built for an ABI this frontend runs,
makes a working factory, binds only buttons its controller declares, and
stamps a version. They need none of the game's files.

## Files the core needs at run time

The package carries none of the game's data and no MT-32 ROM. The user
provides the files of Sword of the Samurai 445.03, the release OpenSamurai is
rebuilt from, and the project brings them as firmware.
`waterbox/waterbox.config` declares each file with its size and SHA-1.

For every project:

- `MISC.EXE`, `MGRAPHIC.EXE`, `START.EXE`, `RP.EXE`, `DUEL.EXE`,
  `BATTLE.EXE`, `MELEE.EXE`.
- `START.CAT`, `RP.CAT`, `DUEL.CAT`, `MELEE.CAT`.
- `FONTS.SAM`, `NSOUND.SAM`, `EGRAPHIC.MEL`, `ICONS.PIC`.

`START.EXE` is the one file that depends on the Release setting: `floppy`
(the default) or `download`. The two are different builds and play the same
game.

By the Sound setting:

- `roland` (the default): `RSOUND.SAM`, and a Roland MT-32's two ROMs,
  `MT32_CONTROL.ROM` (v1.07) and `MT32_PCM.ROM`. The ROMs are no files of
  the game's. Another MT-32 ROM that Munt knows may take their place.
- `adlib`: `ASOUND.SAM`, the download's. The floppy's older AdLib driver is
  refused by name.
- `speaker`: `ISOUND.SAM`.
- `tandy`: `TSOUND.SAM`.
- `none`: nothing more.

Where they come from: the repository names two sources of the game's files,
the original floppy and the download sold on Steam and GOG.com. They differ
only in `START.EXE` and the AdLib driver. `waterbox/waterbox.config`
describes the MT-32's control ROM as dumped from a unit of the first
generation.

A missing file is refused by name. A file of the user's own (a modified one,
the other release's `START.EXE`) may take an original's place: the core
takes it as it is and the project pins its hash.

A project may also add, in its one file slot (`waterbox/file_slots.json`):

- `savedgame`: a `TALLTALE.DAT`, the game's own saved-game file. Without it
  the project starts with the blank file a new installation has.

## Troubleshooting

- `miniBox not found` from the gate or the package script: they fell back to
  `~/chimera/extern/chimera-common-minibox` and it is not there. Pass `-m` or
  export `MINIBOX_DIR`.
- `miniBox's C++ guest toolchain is missing: .../build/meson-cpp/guest-sysroot`:
  miniBox was built without `-Dguest_cpp=true`, or not at all. See
  "Build miniBox".
- `could not download the GCC <version> source from any mirror`, from
  miniBox's C++ toolchain build: the build needs the network to fetch the
  GCC source.
- `extern/OpenSamurai is not checked out`: run
  `git submodule update --init --recursive`. The same command brings Munt,
  which is a submodule of OpenSamurai.
- `the guest build failed (build/package-make.log)` from `build-package.sh`:
  the last twenty lines of that log are printed above the message; the
  whole of it is in the file.
- `build:fresh FAIL` in the gate: `-q` was used on a `core.wbx` older than a
  source. Run the gate without `-q`.
- `nothing built to test`: `-q` was used before anything was built.
- A gate leg fails in the build: the logs are `build/gate/native-make.log`
  and `build/gate/guest-make.log`.
- A `find` message that `patches` is no such file or directory, while the
  gate runs: the gate looks for patches newer than `core.wbx`, and this
  repository has no `patches/` directory. The message is harmless.
- `stacks:map-stack FAIL`: the game's stack must come from `mmap` with
  `MAP_STACK`. Linux runs the core either way; on Windows miniBox cannot
  deliver a fault on a stack page it was not told about.
- The Roland's `audioHash` differs between `run-native` and `run-wbx`: that
  is expected, see "The core gate".
- `packaging is not deterministic`: two archives of the same staging folder
  differed. The package's SHA-1 is the core's identity, so the script stops.
