#!/usr/bin/env python3
"""Writes waterbox.config: what the frontend is told about this core.

The game's files and their hashes come from the list the driver checks at Init
(samurai-driver.c's k_files); the buttons from the driver's wire order
(samurai-driver.h).

usage: gen-config.py [<out>]   (default: waterbox/waterbox.config)
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def game_files():
    """(name, releases, need, size, sha1) as samurai-driver.c's k_files has them."""
    text = open(os.path.join(HERE, "samurai-driver.c")).read()
    return re.findall(r'\{ "([A-Z0-9_]+\.[A-Z]{3})", (REL_\w+), (NEED_\w+), (\d+), "([0-9A-F]{40})" \}', text)


def rom(name):
    return name.endswith(".ROM")


def buttons():
    """The input.buttons names, in the driver's enum order (samurai-driver.h)."""
    text = open(os.path.join(HERE, "samurai-driver.h")).read()
    enum = text[text.index("enum SamButton"):text.index("SAM_BTN_COUNT")]
    enum = re.sub(r"/\*.*?\*/", "", enum, flags=re.S)
    syms = re.findall(r"SAM_BTN_(\w+)", enum)
    special = {"ESC": "Esc", "PLUS": "Keypad +", "MINUS": "Keypad -", "STAR": "Keypad *", "EQUALS": "Equals",
               "F1": "F1", "F2": "F2", "F3": "F3"}
    # the rest: the symbol's words, capitalised (UP_LEFT -> Up Left); a letter or a digit is itself
    return [special.get(s) or (s if len(s) == 1 else " ".join(w.capitalize() for w in s.split("_"))) for s in syms]


WHAT = {
    "MISC.EXE": "the keyboard and joystick helpers every program calls",
    "NSOUND.SAM": "the silent sound driver, which the programs load whatever the setup chose",
    "MGRAPHIC.EXE": "the VGA graphics driver",
    "FONTS.SAM": "the fonts",
    "START.EXE": "the title, the career choices and character creation",
    "RP.EXE": "the role-playing game",
    "DUEL.EXE": "the duels",
    "BATTLE.EXE": "the battles",
    "MELEE.EXE": "the melees",
    "START.CAT": "the title's pictures, the windows' and the political map's data",
    "RP.CAT": "the role-playing game's pictures and the provinces' travel maps",
    "DUEL.CAT": "the duels' backgrounds and fighters",
    "MELEE.CAT": "the melees' pictures",
    "EGRAPHIC.MEL": "the melee's own drawing code (it draws in the EGA's mode)",
    "ICONS.PIC": "the battle's icons",
    "ASOUND.SAM": "the AdLib sound driver",
    "ISOUND.SAM": "the PC speaker's sound driver",
    "TSOUND.SAM": "Tandy's sound driver",
    "RSOUND.SAM": "the Roland MT-32's sound driver",
    "MT32_CONTROL.ROM": "no file of the game's: the Roland MT-32's control ROM, v1.07, dumped from a unit of the first generation - the sound the game was made for",
    "MT32_PCM.ROM": "no file of the game's: the Roland MT-32's PCM ROM, the one every MT-32 has",
}

RELEASE = {
    "REL_FLOPPY": ("the original floppy", ["floppy"]),
    "REL_DOWNLOAD": ("the download sold on Steam and GOG.com", ["download"]),
    "REL_ANY": ("either release", ["floppy", "download"]),
}

SOUND = {"NEED_ADLIB": "adlib", "NEED_SPEAKER": "speaker", "NEED_TANDY": "tandy", "NEED_ROLAND": "roland"}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "waterbox.config")
    firmware = []
    for name, rel, need, size, sha1 in game_files():
        rel_text, rel_values = RELEASE[rel]
        if rom(name):
            desc = "%s: %s. Yours to supply, when the sound is the Roland's - the package carries none of it. Another MT-32 ROM Munt knows may take its place (the project pins its hash)." % (name, WHAT[name])
        else:
            desc = "%s of Sword of the Samurai 445.03 (MicroProse, 1989, DOS), %s's: %s. Yours to supply - the package carries none of the game's data. A file of your own (a modified one) may take its place: the project pins its hash." % (
                name, rel_text, WHAT[name])
        if name == "START.EXE":
            desc += " The floppy's and the download's are different builds; the release setting says which the wizard looks for, and either plays the same game."
        if name == "ASOUND.SAM":
            desc += " The download's (dated 1-10-94), whichever release is played: OpenSamurai's AdLib is rebuilt from it, and the floppy's older driver is refused."
        conds = []
        if rel != "REL_ANY":
            conds.append({"setting": "release", "in": rel_values})
        if need in SOUND:
            conds.append({"setting": "sound", "in": [SOUND[need]]})
        decl = {
            "id": name,
            "display": ("Roland MT-32 %s ROM" % ("control" if "CONTROL" in name else "PCM")) if rom(name)
            else "Sword of the Samurai %s%s" % (name, " (%s)" % rel_values[0] if rel != "REL_ANY" else ""),
            "description": desc,
            "size": int(size),
            "sha1": sha1,
            "name": name,
        }
        if conds:
            decl["requiredWhen"] = conds[0] if len(conds) == 1 else {"all": conds}
        firmware.append(decl)

    cfg = {
        "coreName": "OpenSamurai",
        "kind": "game",
        "systemId": "SwordOfTheSamurai",
        "author": "Sergio Martin, from MicroProse's Sword of the Samurai (1989); chimera port by Sergio Martin",
        "url": "https://github.com/ToolAssisted-run/chimera-core-opensamurai",
        "deterministic": True,
        "memoryLayoutMiB": [32, 8, 8, 8, 32],
        "_memoryLayoutMiB_note": "sbrk, sealed, invisible, plain, mmap. The DOS machine's megabyte and the programs' files are allocated on the heap; the game's stack (8 MB) is mmap'd (MAP_STACK).",
        "video": {
            "_comment": "The DOS game's 320x200 VGA screen (mode 13h; the melee's EGA mode drawn through the VGA), shown on a 4:3 monitor.",
            "width": 320,
            "height": 200,
            "virtualWidth": 320,
            "virtualHeight": 240,
            "vsyncNumerator": 3146875,
            "vsyncDenominator": 44900,
            "_vsync_note": "A frame is one VGA frame (70.086 Hz): the game's programs wait for the retrace, and each reads its keys as it likes.",
            "getBgra": "GetVideoBgra",
        },
        "audio": {
            "_comment": "The sound the setup's driver makes (the AdLib's by default), rendered at 44100 Hz for each frame: the chips' mono on both sides, the Roland MT-32's in stereo.",
            "rate": 44100,
            "samplesPerFrame": 4096,
            "channels": 2,
            "get": "GetAudio",
        },
        "lag": {"inputWasRead": "InputWasRead"},
        "input": {
            "name": "Sword of the Samurai",
            "_comment": "The PC's keyboard, a button for each key the game reads: the eight directions of the numeric keypad (Num Lock off), Enter and Space (the selector), Backspace (the second selector; in a duel, parry), Esc (back), F1 (the Status Scroll), F2 (the Strategic Map), F3 (the Summary Scroll), the battle's orders (keypad + and =: turn and march, keypad -: march, keypad *: turn), the digits (the battle's units), the letters (the samurai's name; R retreats from a battle) with Shift for capitals, and the game's commands: Sound (Alt+V) and Graphics (Alt+Z). Left out: a campaign can be neither saved, restored, abandoned nor quit (Alt+S, Alt+R, Alt+N - which leads back to the career choices and their Restore - and Alt+Q), and Alt+J is the joystick, which the machine does not have. A saved game to start from is the project's saved-game file, restored from the career choices. A key held repeats as a keyboard's does: after half a second, 10.9 a second.",
            "buttons": buttons(),
        },
        "settings": [
            {
                "name": "release",
                "display": "Release",
                "type": "enum",
                "options": ["floppy", "download"],
                "default": "floppy",
                "description": "The release of Sword of the Samurai 445.03 the project's files are: the original floppy's (the one the published speedrun plays), or the download sold on Steam and GOG.com. They are the same game; only START.EXE (whose code OpenSamurai does not run) and the AdLib driver differ.",
            },
            {
                "name": "sound",
                "display": "Sound",
                "type": "enum",
                "options": ["roland", "adlib", "speaker", "tandy", "none"],
                "default": "roland",
                "description": "The sound device the setup chose: the Roland MT-32 (the default), the AdLib, the IBM PC speaker, Tandy's, or none. The AdLib needs the download's ASOUND.SAM (OpenSamurai's AdLib is rebuilt from it; the floppy's is an older driver). The Roland needs RSOUND.SAM and an MT-32's two ROMs (v1.07, the first generation the game was made for), which the project brings as firmware. With the speaker the title runs slower, as it did.",
            },
            {
                "name": "skip_title",
                "display": "Skip the Title",
                "type": "bool",
                "default": False,
                "description": "Start with /NT, as the original allowed: the title sequence is left out and the game starts at the copy protection's question.",
            },
            {
                "name": "random_seed",
                "display": "Random Seed",
                "type": "string",
                "default": "0",
                "description": "The initial seed every program's random numbers are drawn from: the crest question, the characters, the encounters, the duels', melees' and battles' foes. The DOS programs took theirs from the clock; OpenSamurai draws each from this one seed. Any 64-bit number, decimal or 0x hexadecimal, as OpenSamurai's own frontend takes it (OPENSAMURAI_SEED): a seed it printed draws the same numbers here. A movie records the seed it ran with.",
            },
            {
                "name": "clock_start",
                "display": "Start Date and Time",
                "type": "string",
                "default": "1989-10-25 12:00:00",
                "description": "The PC's clock when the game starts, YYYY-MM-DD HH:MM:SS; it runs on with the frames, and the game reads it as the time (how long a prompt waits, when a travelling encounter may come). A movie records the start it ran with.",
            },
        ],
        "firmware": firmware,
    }
    with open(out, "w") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
