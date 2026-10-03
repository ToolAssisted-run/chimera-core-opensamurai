#!/bin/bash
# The core gate. The sandboxed core must play Sword of the Samurai exactly as
# the native reference does (the same driver, OpenSamurai and Munt built for
# the host) - picture, sound, every step's length, the clock and every memory
# domain - survive a savestate before every step and a new host in the middle
# of a run, and then:
#   - show the title, the copy protection's crest question and the career
#     choices as they are, and run a duel, a melee and a battle (the career
#     choices' practice encounters)
#   - take its settings: the release, each sound device (the Roland's MT-32
#     with its ROMs), the title skipped, the random seed
#   - type as a keyboard does: a key typed once, Shift for capitals, a key held
#     repeated after half a second and not before
#   - start from the blank saved-game file a new installation has, or from the
#     project's own, and restore from it
#   - have no Save, Restore, New Game or Quit (a campaign is neither saved,
#     restored, abandoned nor quit), and change the sound with its command
#   - export a property table that holds to chimera's docs/game-cores.md, and
#     obey a poke and hold a freeze through it
#   - refuse a missing game file and the floppy's AdLib driver, each by
#     name, and take a file of the project's own (a changed one, the other
#     release's START.EXE) in the original's place
#   - ask for its game stack as a stack (MAP_STACK)
#   - package deterministically
#
# The game is the user's Sword of the Samurai 445.03, never in the repository:
# the gate takes it from tests/roms-local (the download's files; the floppy's
# in tests/roms-local/floppy, the MT-32's ROMs in tests/roms-local/roland), or
# -d <dir>. Without it only the build, the declarations and the refusal of a
# project with no files run.
#
# Usage: ./run-gate.sh [-q] [-m <miniBox dir>] [-d <game dir>]
#   -q skips the build (uses what is built)
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
data="${SAMURAI_DIR:-$root/tests/roms-local}"
quick=0
while getopts "qm:d:" opt; do
	case "$opt" in
		q) quick=1 ;;
		m) mb="$OPTARG" ;;
		d) data="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
mb="$(cd "$mb" 2>/dev/null && pwd)" || { echo "miniBox not found; pass -m or set MINIBOX_DIR" >&2; exit 1; }

nat="$root/build/native"
wbx="$root/build/guest/core.wbx"
work="$root/build/gate"
rm -rf "$work"
mkdir -p "$work"

ok=0
failed=0
skipped=0
report() {
	printf "%-30s %-6s %s\n" "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok+1)) ;; SKIP) skipped=$((skipped+1)) ;; *) failed=$((failed+1)) ;; esac
}
printf "%-30s %-6s %s\n" "Check" "Result" "Detail"
printf "%-30s %-6s %s\n" "-----" "------" "------"

digests() { grep -E '^(frames|vsync|videoHash|audioHash|stepsHash|lagFrames|clock|domain\[)'; }
# what a turbo run can be held to: all but the whole-run picture hash, which a
# run that skipped half its conversions cannot match - the half it drew is
# compared instead
turboDigests() { grep -E '^(frames|vsync|tailVideoHash|audioHash|stepsHash|lagFrames|clock|domain\[)'; }
# the MT-32's sound is not held to the native reference: Munt builds its tables
# with floating point, and glibc's libm and musl's round differently (the
# DOSBox-X core's open item). The sandbox, which is what Chimera runs, is held
# to itself; natively everything else is compared
noAudio() { grep -vE '^audioHash='; }
# a program that stops stepping would spin forever; no run here takes minutes
native() { timeout 600 "$nat/run-native" "$@"; }
boxed() { timeout 600 "$nat/run-wbx" "$wbx" "$@"; }
# the property at a step of a trace (column 4 on is --trace-props, in order)
at() { awk -v s="$2" -v c="$3" '$1 == s { print $(3 + c) }' "$1"; }
pixels() { tail -c +19 "$1" | sha1sum | cut -c1-16; }

# ------------------------------------------------------------------ 1. build
if [ "$quick" -eq 0 ]; then
	if make -C "$here" -f native.mk MB="$mb" -j"$(nproc)" > "$work/native-make.log" 2>&1 &&
	   make -C "$here" -f guest.mk MB="$mb" -j"$(nproc)" > "$work/guest-make.log" 2>&1; then
		report "build" PASS "native reference, harnesses and core.wbx (check-wbx clean)"
	else
		report "build" FAIL "see build/gate/*-make.log"
	fi
fi
[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] && [ -f "$wbx" ] || { echo "nothing built to test" >&2; exit 1; }
# a gate over an old core.wbx proves nothing about the sources: every source
# and the patch series must be older than what is tested
stale="$(find "$here" -maxdepth 1 \( -name '*.c' -o -name '*.h' -o -name '*.mk' \) \
	! -name 'run-*.c' ! -name 'gate-harness.h' -newer "$wbx" | head -3; find "$root/patches" -name '*.patch' -newer "$wbx" | head -1)"
if [ -n "$stale" ]; then
	report "build:fresh" FAIL "core.wbx is older than $(echo $stale | tr '\n' ' ')"
fi

# The game runs on a stack of its own, which must be asked for as a stack
# (mmap, MAP_STACK): on Windows miniBox cannot deliver a fault on a page the
# stack pointer is in unless it was told the page is a stack, and the x68k core
# died there on its first frame. Linux runs the core either way.
if nm "$root/build/guest/core/coro.o" 2>/dev/null | grep -q ' U mmap$'; then
	report "stacks:map-stack" PASS "the game's stack is mmap'd (MAP_STACK), not malloc'd"
else
	report "stacks:map-stack" FAIL "coro.o does not take its stack from mmap"
fi

# ------------------------------------------------------------------ 2. the declarations
if python3 "$here/tests/check-wire.py" "$root" > "$work/wire.txt" 2>&1; then
	report "wire:config==driver" PASS "$(cat "$work/wire.txt")"
else
	report "wire:config==driver" FAIL "$(tail -1 "$work/wire.txt")"
fi
# a campaign can be neither saved, restored, abandoned nor quit: none of the
# game's Alt+S, Alt+R, Alt+N or Alt+Q is a button (user-decided, 2026-09-29)
if python3 - "$here/waterbox.config" > "$work/nomenu.txt" 2>&1 <<'EOF'
import json, sys
b = json.load(open(sys.argv[1]))["input"]["buttons"]
banned = [n for n in b if n in ("Save Game", "Restore Game", "New Game", "Quit")]
assert not banned, banned
assert "Sound" in b and "Graphics" in b, b
print(f"{len(b)} buttons; the game's commands are Sound and Graphics only")
EOF
then
	report "buttons:no-campaign-menu" PASS "$(cat "$work/nomenu.txt")"
else
	report "buttons:no-campaign-menu" FAIL "$(tail -1 "$work/nomenu.txt")"
fi

# a work dir: the game files the project would mount, and a settings file
FILES="MISC.EXE NSOUND.SAM MGRAPHIC.EXE FONTS.SAM RP.EXE DUEL.EXE BATTLE.EXE MELEE.EXE START.CAT RP.CAT DUEL.CAT MELEE.CAT EGRAPHIC.MEL ICONS.PIC ASOUND.SAM ISOUND.SAM TSOUND.SAM RSOUND.SAM"
# The gate's runs are the AdLib's unless they name the sound: the Roland, the
# default, is not held to the native reference - its own run says roland, and
# settings:sound-default what a project that names no sound gets.
workdir() {
	local wd="$work/$1" s="$2"
	mkdir -p "$wd"
	for f in $FILES; do [ -f "$data/$f" ] && cp "$data/$f" "$wd/"; done
	# the floppy's START.EXE: the release setting's default
	[ -f "$data/floppy/START.EXE" ] && cp "$data/floppy/START.EXE" "$wd/"
	for f in "$data"/roland/*.ROM; do [ -f "$f" ] && cp "$f" "$wd/"; done
	case "$s" in *'"sound"'*) ;; '{}') s='{"sound":"adlib"}' ;; *) s="{\"sound\":\"adlib\",${s#\{}" ;; esac
	printf '%s' "$s" > "$wd/settings"
	echo "$wd"
}

# ------------------------------------------------------------------ 3. no files
wd="$work/nofiles"; mkdir -p "$wd"; printf '{}' > "$wd/settings"
boxed "$wd" --frames 1 > "$work/nofiles.txt" 2>/dev/null
if grep -qx 'loadError=Sword of the Samurai needs MISC.EXE - add it as the project.s firmware.' "$work/nofiles.txt"; then
	report "refuse:no-files" PASS "$(sed -n 's/^loadError=//p' "$work/nofiles.txt")"
else
	report "refuse:no-files" FAIL "$(head -1 "$work/nofiles.txt")"
fi

if [ ! -f "$data/RP.EXE" ] || [ ! -f "$data/floppy/START.EXE" ]; then
	report "game" SKIP "no Sword of the Samurai files in $data (and the floppy's START.EXE in $data/floppy)"
	echo; echo "$ok ok, $failed failed, $skipped skipped"
	[ "$failed" -eq 0 ]; exit
fi

# ------------------------------------------------------------------ 4. refusals
wd="$(workdir refuse-missing '{}')"; rm "$wd/RP.CAT"
boxed "$wd" --frames 1 > "$work/r1.txt" 2>/dev/null
native "$wd" --frames 1 > "$work/r1n.txt" 2>/dev/null
if grep -qx 'loadError=Sword of the Samurai needs RP.CAT - add it as the project.s firmware.' "$work/r1.txt" && cmp -s <(grep loadError "$work/r1.txt") <(grep loadError "$work/r1n.txt"); then
	report "refuse:missing-file" PASS "$(sed -n 's/^loadError=//p' "$work/r1.txt")"
else
	report "refuse:missing-file" FAIL "$(grep -m1 . "$work/r1.txt")"
fi

# a file of the project's own is taken in the original's place (Chimera pins
# its hash; user-decided 2026-09-29): a START.CAT with 64 bytes changed plays,
# and the title's pictures are its
wd="$(workdir custom-orig '{}')"
orig="$(boxed "$wd" --frames 400 2>/dev/null | grep -E '^(loadError|frames|videoHash)')"
wd="$(workdir custom-file '{}')"
python3 -c "import sys; p=sys.argv[1]; d=bytearray(open(p,'rb').read()); d[63488:63552]=bytes(b ^ 0x55 for b in d[63488:63552]); open(p,'wb').write(d)" "$wd/START.CAT"
custom="$(boxed "$wd" --frames 400 2>/dev/null | grep -E '^(loadError|frames|videoHash)')"
if ! echo "$custom$orig" | grep -q loadError && echo "$custom" | grep -qx 'frames=400' &&
   [ "$(echo "$custom" | grep videoHash)" != "$(echo "$orig" | grep videoHash)" ]; then
	report "firmware:custom" PASS "a START.CAT of the project's own (64 bytes changed) is taken, and the title's pictures are its"
else
	report "firmware:custom" FAIL "custom [$(echo $custom)] original [$(echo $orig)]"
fi

if [ -f "$data/floppy/ASOUND.SAM" ]; then
	wd="$(workdir refuse-floppy-adlib '{}')"; cp "$data/floppy/ASOUND.SAM" "$wd/ASOUND.SAM"
	boxed "$wd" --frames 1 > "$work/r3.txt" 2>/dev/null
	if grep -q "^loadError=This ASOUND.SAM is the original floppy's AdLib driver, an older build" "$work/r3.txt"; then
		report "refuse:floppy-adlib" PASS "the floppy's older AdLib driver refused by name"
	else
		report "refuse:floppy-adlib" FAIL "$(grep -m1 . "$work/r3.txt")"
	fi
else
	report "refuse:floppy-adlib" SKIP "no floppy ASOUND.SAM in $data/floppy"
fi

# ...and the other release's START.EXE too, whose code OpenSamurai does not
# run: the download's under the floppy setting plays the same title
if [ -f "$data/START.EXE" ]; then
	wd="$(workdir other-release '{}')"; cp "$data/START.EXE" "$wd/START.EXE"
	other="$(boxed "$wd" --frames 400 2>/dev/null | grep -E '^(loadError|frames|videoHash)')"
	if [ -n "$other" ] && [ "$other" = "$orig" ] && ! cmp -s "$data/START.EXE" "$data/floppy/START.EXE"; then
		report "firmware:other-release" PASS "the download's START.EXE under the floppy setting: taken, the same 400 steps"
	else
		report "firmware:other-release" FAIL "[$(echo $other)] vs [$(echo $orig)]"
	fi
else
	report "firmware:other-release" SKIP "no download START.EXE in $data"
fi

# ------------------------------------------------------------------ 5. the runs
# (since OpenSamurai 887b992 the title's two dissolves take the original's 116
# frames each, where they were instant, and since b40625a the first stands 2
# seconds for the original's calibration: the title's picture is up by step 740,
# and everything after the title comes 350 steps later than it did - its
# 7-second hold ends on a whole second of the clock - with the same pictures)
# the career choices' encounters: Enter at the crest question, then Down to the
# encounter (40 frames apart: closer presses are the keyboard joystick's double
# tap, which the menu takes as two), Enter, then Enter through the difficulty
# and the story's pages
enc() { local n=$1 at=2750; for i in $(seq 1 "$n"); do echo --press $at:D:1; at=$((at + 40)); done; for k in 0 200 400 600; do echo --press $((at + k)):N:1; done; }
# name, steps, settings, what the run does
tests=(
	"title|1950|{}|title"
	"duel|5550|{}|duel"
	"melee|5550|{}|melee"
	"battle|5550|{}|battle"
	"roland|1950|{\"sound\":\"roland\"}|title"
)
test_args() {
	case "$1" in
		title) args=(--screenshot "740:$work/$2-title.tga" --screenshot "1849:$work/$2-crest.tga") ;;
		duel) args=(--press 1850:N:1 $(enc 3) --screenshot "2649:$work/career.tga" --screenshot "5549:$work/duel.tga") ;;
		melee) args=(--press 1850:N:1 $(enc 5) --screenshot "3950:$work/melee.tga") ;;
		battle) args=(--press 1850:N:1 $(enc 7) --screenshot "5549:$work/battle.tga") ;;
	esac
}
for t in "${tests[@]}"; do
	IFS='|' read -r name frames settings what <<< "$t"
	wd="$(workdir "$name" "$settings")"
	test_args "$what" "$name"
	args+=(--frames "$frames")
	cmpnat=digests
	[ "$name" = roland ] && cmpnat=noAudio

	if ! native "$wd" "${args[@]}" > "$work/$name.native.txt" 2> "$work/$name.native.err"; then
		report "$name:equivalence" FAIL "native runner: $(tail -1 "$work/$name.native.err")"; continue
	fi
	if ! boxed "$wd" "${args[@]}" --props-json "$work/$name.box.json" > "$work/$name.box.txt" 2> "$work/$name.box.err"; then
		report "$name:equivalence" FAIL "sandbox runner: $(tail -1 "$work/$name.box.err")"; continue
	fi
	digests < "$work/$name.native.txt" | $cmpnat > "$work/nat.txt"
	digests < "$work/$name.box.txt" | $cmpnat > "$work/boxcmp.txt"
	digests < "$work/$name.box.txt" > "$work/box.txt"
	boxed "$wd" "${args[@]}" 2>/dev/null | digests > "$work/again.txt"
	if cmp -s "$work/box.txt" "$work/again.txt"; then
		report "$name:determinism" PASS "a second sandboxed run is the same"
	else
		report "$name:determinism" FAIL "$(diff "$work/box.txt" "$work/again.txt" | tr '\n' ' ' | head -c 110)"
	fi
	if cmp -s "$work/nat.txt" "$work/boxcmp.txt"; then
		report "$name:equivalence" PASS "$frames steps, native == sandboxed ($(grep -c . "$work/boxcmp.txt") digests$([ "$name" = roland ] && echo '; the MT-32 sound held to the sandbox'))"
	else
		report "$name:equivalence" FAIL "$(diff "$work/nat.txt" "$work/boxcmp.txt" | tr '\n' ' ' | head -c 110)"; continue
	fi

	boxed "$wd" "${args[@]}" 2>/dev/null | turboDigests > "$work/tnorm.txt"
	boxed "$wd" "${args[@]}" --turbo 2>/dev/null | turboDigests > "$work/turbo.txt"
	if cmp -s "$work/tnorm.txt" "$work/turbo.txt"; then
		report "$name:turbo" PASS "half the pictures unconverted, same game and same second half"
	else
		report "$name:turbo" FAIL "$(diff "$work/tnorm.txt" "$work/turbo.txt" | tr '\n' ' ' | head -c 110)"
	fi

	boxed "$wd" "${args[@]}" --rerecord 2>/dev/null | digests > "$work/rr.txt"
	if cmp -s "$work/box.txt" "$work/rr.txt"; then
		report "$name:savestate" PASS "saved and loaded before every step: lossless"
	else
		report "$name:savestate" FAIL "$(diff "$work/box.txt" "$work/rr.txt" | tr '\n' ' ' | head -c 110)"
	fi

	boxed "$wd" "${args[@]}" --session 2> "$work/ss.err" | digests > "$work/ss.txt"
	if cmp -s "$work/box.txt" "$work/ss.txt"; then
		report "$name:session" PASS "a new host finished the run from a state at step $((frames / 2))"
	else
		report "$name:session" FAIL "$(diff "$work/box.txt" "$work/ss.txt" | tr '\n' ' ' | head -c 110)"
	fi
done

# ------------------------------------------------------------------ 6. what the runs showed
# the pictures, as the gate last saw them (look at build/gate/*.png)
png() { python3 "$here/tests/tga2png.py" "$work/$1.tga" "$work/$1.png" 2 2>/dev/null; }
for shot in "title-title:740 (the title):f4d10c76eaf325c4" "title-crest:1849 (the crest question):5192f8983129a02b" \
	"career:2649 (the career choices):a546bdc3547a163a" "duel:5549 (kenjutsu training):4855d71a8e729c30" \
	"melee:3950 (the outpost of Ishiyama Hongan-ji):fdcabebe6c9702dd" "battle:5549 (a skirmish):07d7dc4494cfc56b"; do
	IFS=':' read -r file what want <<< "$shot"
	png "$file"
	got="$(pixels "$work/$file.tga")"
	if [ "$got" = "$want" ]; then
		report "picture:$file" PASS "step $what is the picture it was (build/gate/$file.png)"
	else
		report "picture:$file" FAIL "step $what: pixels $got, expected $want (build/gate/$file.png)"
	fi
done

# the melee thinks it is on the fast machine the oracle was: its start-up speed
# test counted enough passes (DS:342A = 0, 1 on a slow machine) - which it does
# only if a read of its tick counter costs what the host says it does
# (GameHost.meleeTickRead)
boxed "$work/melee" --frames 3951 --press 1850:N:1 $(enc 5) --dump-domain "Conventional Memory" "$work/melee-at-3950.bin" > /dev/null 2>&1
fast="$(python3 -c "import sys; print(open(sys.argv[1], 'rb').read()[0x3886 * 16 + 0x342A])" "$work/melee-at-3950.bin" 2>/dev/null)"
if [ "$fast" = 0 ]; then
	report "melee:fast-machine" PASS "the speed test found the fast machine (MELEE DS:342A = 0)"
else
	report "melee:fast-machine" FAIL "MELEE DS:342A = ${fast:-unread} (1: the slow machine's melee)"
fi

# the MT-32 plays: its sound is stereo (the chips' is the same on both sides),
# and the title's music is not the AdLib's
if python3 - "$wbx" "$work/roland" "$nat/run-wbx" > "$work/mt32.txt" 2>&1 <<'EOF'
import struct, subprocess, sys
wbx, wd, run = sys.argv[1:]
subprocess.run([run, wbx, wd, "--frames", "900", "--audio", wd + "/a.raw"], capture_output=True, check=True)
d = open(wd + "/a.raw", "rb").read()
s = struct.unpack("<%dh" % (len(d) // 2), d)
stereo = sum(1 for i in range(0, len(s), 2) if s[i] != s[i + 1])
peak = max(abs(x) for x in s)
assert stereo > 1000 and peak > 1000, (stereo, peak)
print(f"900 steps: {len(s) // 2} sound frames, {stereo} of them with left and right apart, peak {peak}")
EOF
then
	report "sound:mt32" PASS "$(cat "$work/mt32.txt")"
else
	report "sound:mt32" FAIL "$(tail -1 "$work/mt32.txt")"
fi

# ------------------------------------------------------------------ 7. the settings
# each sound device reaches the game: the same pictures (the speaker's aside,
# with which the title runs slower, as it did) and each its own sound
sounds=""
for s in adlib speaker tandy none; do
	wd="$(workdir "sound-$s" "{\"sound\":\"$s\"}")"
	boxed "$wd" --frames 1950 2>/dev/null | grep -E '^(videoHash|audioHash)' > "$work/sound-$s.txt"
	sounds="$sounds $s"
done
v() { sed -n "s/^videoHash=//p" "$work/sound-$1.txt"; }
a() { sed -n "s/^audioHash=//p" "$work/sound-$1.txt"; }
if [ "$(v adlib)" = "$(v tandy)" ] && [ "$(v adlib)" = "$(v none)" ] && [ "$(v adlib)" != "$(v speaker)" ] &&
   [ "$(printf '%s\n' "$(a adlib)" "$(a speaker)" "$(a tandy)" "$(a none)" "$(sed -n 's/^audioHash=//p' "$work/roland.box.txt")" | sort -u | wc -l)" = 5 ]; then
	report "settings:sound" PASS "adlib, speaker, tandy, roland and none: five sounds; the pictures the same but the speaker's slower title"
else
	report "settings:sound" FAIL "video adlib $(v adlib) speaker $(v speaker) tandy $(v tandy) none $(v none)"
fi
# the Roland is the default: a project that names no sound plays the roland
# run's sound, and without an MT-32 ROM is refused for it
wd="$(workdir sound-default '{"sound":"x"}')"; printf '{}' > "$wd/settings"
dflt="$(boxed "$wd" --frames 1950 2>/dev/null | sed -n 's/^audioHash=//p')"
rm "$wd/MT32_PCM.ROM"
boxed "$wd" --frames 1 > "$work/sound-default.txt" 2>/dev/null
if [ -n "$dflt" ] && [ "$dflt" = "$(sed -n 's/^audioHash=//p' "$work/roland.box.txt")" ] && [ "$dflt" != "$(a adlib)" ] &&
   grep -qx "loadError=Sword of the Samurai needs MT32_PCM.ROM - add it as the project's firmware." "$work/sound-default.txt"; then
	report "settings:sound-default" PASS "no sound setting: the Roland's sound (the roland run's, 1950 steps), and the MT-32's ROMs asked for"
else
	report "settings:sound-default" FAIL "default $dflt, roland $(sed -n 's/^audioHash=//p' "$work/roland.box.txt"), adlib $(a adlib); $(grep -m1 . "$work/sound-default.txt")"
fi

# the release: the download's START.EXE plays the same game (a different image
# of its code, which OpenSamurai does not run, in memory)
wd="$(workdir release-download '{"release":"download"}')"; cp "$data/START.EXE" "$wd/START.EXE"
boxed "$wd" --frames 1950 2>/dev/null | grep -E '^(videoHash|audioHash)' > "$work/rel-download.txt"
if cmp -s "$work/rel-download.txt" <(grep -E '^(videoHash|audioHash)' "$work/title.box.txt"); then
	report "settings:release" PASS "the download's START.EXE: the same pictures and sound as the floppy's"
else
	report "settings:release" FAIL "$(tr '\n' ' ' < "$work/rel-download.txt")"
fi

# the title skipped (/NT), and the random seed: each another crest question
wd="$(workdir notitle '{"skip_title":true}')"
boxed "$wd" --frames 1950 --screenshot "400:$work/notitle.tga" 2>/dev/null > /dev/null
wd="$(workdir seed '{"random_seed":1234}')"
boxed "$wd" --frames 1950 --screenshot "1849:$work/seed-crest.tga" 2>/dev/null > /dev/null
png notitle; png seed-crest
if [ "$(pixels "$work/notitle.tga")" = "$(pixels "$work/seed-crest.tga")" ] 2>/dev/null; then :; fi
if [ "$(pixels "$work/notitle.tga")" != "$(pixels "$work/title-title.tga")" ] &&
   [ "$(pixels "$work/seed-crest.tga")" != "$(pixels "$work/title-crest.tga")" ]; then
	report "settings:title-and-seed" PASS "no title: the credits by step 400, not the title; seed 1234: another crest asked (build/gate/notitle.png, seed-crest.png)"
else
	report "settings:title-and-seed" FAIL "notitle $(pixels "$work/notitle.tga"), seed crest $(pixels "$work/seed-crest.tga")"
fi

# the seed is OpenSamurai's own: any 64-bit number, decimal or 0x hexadecimal
# (OPENSAMURAI_SEED's strtoull) - 0x4D2 is 1234, the largest is another game,
# and what is not a number is refused
seedrun() { local wd; wd="$(workdir "seed-$1" "{\"random_seed\":\"$2\"}")"; boxed "$wd" --frames 1850 2>/dev/null | grep -E '^(loadError|videoHash)'; }
s_dec="$(seedrun dec 1234)"; s_hex="$(seedrun hex 0x4D2)"; s_max="$(seedrun max 18446744073709551615)"; s_bad="$(seedrun bad 12ab)"
s_32="$(seedrun low32 4294967295)"
if [ "$s_dec" = "$s_hex" ] && [ -n "$s_max" ] && [ "$s_max" != "$s_dec" ] && [ "$s_max" != "$s_32" ] && [ "${s_max#videoHash=}" != "$s_max" ] &&
   [ "$s_bad" = 'loadError=the random seed is "12ab"; it is a number from 0 to 18446744073709551615, or 0x and hexadecimal digits' ]; then
	report "settings:seed-64-bit" PASS "1234 == 0x4D2; 18446744073709551615 another game, not 4294967295's (all 64 bits count); \"12ab\" refused"
else
	report "settings:seed-64-bit" FAIL "dec [$s_dec] hex [$s_hex] max [$s_max] bad [$s_bad]"
fi

# ------------------------------------------------------------------ 8. the keyboard
# a key typed once, and held: before half a second it is still one key, after
# it repeats (the career choices' cursor: one item, one item, four)
wd="$(workdir keys '{}')"
for h in 1 30 90; do boxed "$wd" --frames 3050 --press 1850:N:1 --press 2750:D:$h --screenshot "3049:$work/hold$h.tga" > /dev/null 2>&1; done
if [ "$(pixels "$work/hold1.tga")" = "$(pixels "$work/hold30.tga")" ] && [ "$(pixels "$work/hold1.tga")" != "$(pixels "$work/hold90.tga")" ]; then
	report "keys:repeat" PASS "Down held 30 steps is one key, 90 steps repeats (build/gate/hold90.png)"
else
	report "keys:repeat" FAIL "tap $(pixels "$work/hold1.tga") / 30 $(pixels "$work/hold30.tga") / 90 $(pixels "$work/hold90.tga")"
fi
png hold90
# the samurai's name, typed at character creation with Shift for its capital
boxed "$wd" --frames 3650 --press 1850:N:1 --press 2750:N:1 --press 3150:S:25 --press 3160:k:1 --press 3190:i:1 \
	--press 3210:r:1 --press 3230:o:1 --press 3300:N:1 --dump-domain "Shared Block" "$work/name.bin" > /dev/null 2>&1
name="$(python3 -c "import sys; print(open(sys.argv[1], 'rb').read()[0x2D4:0x2D4 + 21].split(b'\0')[0].decode())" "$work/name.bin" 2>/dev/null)"
if [ "$name" = "Kiro" ]; then
	report "keys:name" PASS "Shift+K, i, r, o, Enter: the samurai is \"$name\" (Samurai.Name)"
else
	report "keys:name" FAIL "the name is \"$name\""
fi

# ------------------------------------------------------------------ 9. the saved games
# Restore Saved Game without a project's file: the blank file a new
# installation has, the floppy's own (7,650 zeros); with one, its saves
restore=(--frames 3750 --press 1850:N:1 --press 2750:D:1 --press 2850:N:1)
wd="$(workdir saved-none '{}')"
boxed "$wd" "${restore[@]}" --screenshot "3749:$work/restore-blank.tga" --dump-domain "Saved Games" "$work/saved-blank.bin" > /dev/null 2>&1
wd="$(workdir saved-floppy '{}')"; cp "$data/floppy/TALLTALE.DAT" "$wd/FLOPPY.DAT"; printf '{"savedgame": ["FLOPPY.DAT"]}' > "$wd/slots"
boxed "$wd" "${restore[@]}" --screenshot "3749:$work/restore-floppy.tga" > /dev/null 2>&1
saves_ok=1
if [ -f "$data/TALLTALE.DAT" ] && cmp -s "$data/TALLTALE.DAT" "$data/floppy/TALLTALE.DAT"; then saves_ok=0; fi
if [ -f "$data/TALLTALE.DAT" ]; then
	wd="$(workdir saved-own '{}')"; cp "$data/TALLTALE.DAT" "$wd/MINE.DAT"; printf '{"savedgame": ["MINE.DAT"]}' > "$wd/slots"
	boxed "$wd" "${restore[@]}" --screenshot "3749:$work/restore-own.tga" --dump-domain "Saved Games" "$work/saved-own.bin" > /dev/null 2>&1
	png restore-own
else
	saves_ok=0
fi
png restore-blank
if [ "$saves_ok" = 1 ] && [ "$(pixels "$work/restore-blank.tga")" = "$(pixels "$work/restore-floppy.tga")" ] &&
   cmp -s "$work/saved-blank.bin" "$data/floppy/TALLTALE.DAT" && cmp -s "$work/saved-own.bin" "$data/TALLTALE.DAT" &&
   [ "$(pixels "$work/restore-own.tga")" != "$(pixels "$work/restore-blank.tga")" ]; then
	report "saved-games" PASS "no file: the floppy's blank one, byte for byte, restored alike; the project's own: its saves listed"
else
	report "saved-games" FAIL "blank $(pixels "$work/restore-blank.tga") floppy $(pixels "$work/restore-floppy.tga") own $(pixels "$work/restore-own.tga" 2>/dev/null)"
fi

# the Sound command, in the role-playing game (a saved game restored): music
# and effects, effects, silence
if [ -f "$data/TALLTALE.DAT" ]; then
	wd="$(workdir sound-command '{}')"; cp "$data/TALLTALE.DAT" "$wd/MINE.DAT"; printf '{"savedgame": ["MINE.DAT"]}' > "$wd/slots"
	boxed "$wd" --frames 4750 --press 1850:N:1 --press 2750:D:1 --press 2850:N:1 --press 3750:N:1 --press 4350:V:1 --press 4550:V:1 \
		--trace "$work/sound-command.trace" --trace-props "Options.Sound Mode" > /dev/null 2>&1
	if [ "$(at "$work/sound-command.trace" 4349 1)" = 0 ] && [ "$(at "$work/sound-command.trace" 4351 1)" = 1 ] && [ "$(at "$work/sound-command.trace" 4551 1)" = 2 ]; then
		report "command:sound" PASS "Alt+V in the role-playing game: music and effects, effects, silence (Options.Sound Mode 0 -> 1 -> 2)"
	else
		report "command:sound" FAIL "sound mode $(at "$work/sound-command.trace" 4349 1) $(at "$work/sound-command.trace" 4351 1) $(at "$work/sound-command.trace" 4551 1)"
	fi
else
	report "command:sound" SKIP "no saved game (TALLTALE.DAT) in $data"
fi

# ------------------------------------------------------------------ 10. the properties
if python3 "$here/tests/check-properties.py" "$work/title.box.json" "$work/title.box.txt" > "$work/props.txt" 2>&1; then
	report "properties:table" PASS "$(cat "$work/props.txt")"
else
	report "properties:table" FAIL "$(tail -1 "$work/props.txt")"
fi
wd="$(workdir poke '{}')"
boxed "$wd" --frames 400 --poke "100:Options.Sound Mode=2" --trace "$work/poke.trace" --trace-props "Options.Sound Mode" > /dev/null 2>&1
# (from step 1: the first step is the launcher's, whose setup writes the block)
boxed "$wd" --frames 400 --freeze "1-399:Options.Sound Mode=1" --trace "$work/freeze.trace" --trace-props "Options.Sound Mode" > /dev/null 2>&1
if [ "$(at "$work/poke.trace" 99 1)" = 0 ] && [ "$(at "$work/poke.trace" 100 1)" = 2 ] && [ "$(at "$work/poke.trace" 399 1)" = 2 ] &&
   [ "$(awk '$1 ~ /^[0-9]+$/ && $1 >= 1 && $4 != 1' "$work/freeze.trace" | wc -l)" = 0 ]; then
	report "properties:poke-freeze" PASS "Options.Sound Mode poked to 2 at step 100 stays; frozen at 1, it is 1 at every step"
else
	report "properties:poke-freeze" FAIL "poke $(at "$work/poke.trace" 99 1) -> $(at "$work/poke.trace" 100 1); freeze off on $(awk '$1 ~ /^[0-9]+$/ && $1 >= 1 && $4 != 1' "$work/freeze.trace" | wc -l) steps"
fi

# ------------------------------------------------------------------ 11. the package
if sh "$here/build-package.sh" -m "$mb" -o "$work/pkg1" > "$work/pkg1.log" 2>&1 &&
   sh "$here/build-package.sh" -m "$mb" -o "$work/pkg2" > "$work/pkg2.log" 2>&1 &&
   cmp -s "$work/pkg1/opensamurai.chimeraCore" "$work/pkg2/opensamurai.chimeraCore"; then
	report "package" PASS "$(grep 'package sha1' "$work/pkg1.log"), the same twice"
else
	report "package" FAIL "see build/gate/pkg*.log"
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
