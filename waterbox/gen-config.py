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
    "MISC.EXE": "the keyboard and joystick routines that every program of the game uses",
    "NSOUND.SAM": "the silent sound driver, which the programs load whichever sound device is chosen",
    "MGRAPHIC.EXE": "the VGA graphics driver",
    "FONTS.SAM": "the fonts",
    "START.EXE": "the title, the career choices and character creation",
    "RP.EXE": "the role-playing part of the game",
    "DUEL.EXE": "the duels",
    "BATTLE.EXE": "the battles",
    "MELEE.EXE": "the melees",
    "START.CAT": "the title pictures and the data of the windows and the political map",
    "RP.CAT": "the pictures of the role-playing part and the travel maps of the provinces",
    "DUEL.CAT": "the backgrounds and fighters of the duels",
    "MELEE.CAT": "the pictures of the melees",
    "EGRAPHIC.MEL": "the drawing code of the melees, which use the EGA graphics mode",
    "ICONS.PIC": "the icons of the battles",
    "ASOUND.SAM": "the AdLib sound driver",
    "ISOUND.SAM": "the PC speaker sound driver",
    "TSOUND.SAM": "the Tandy sound driver",
    "RSOUND.SAM": "the Roland MT-32 sound driver",
    "MT32_CONTROL.ROM": "the control ROM of the Roland MT-32, version 1.07, dumped from a unit of the first generation, which is the sound the game was made for",
    "MT32_PCM.ROM": "the PCM ROM of the Roland MT-32, which is the same in every MT-32",
}

RELEASE = {
    "REL_FLOPPY": ("the original floppy release", ["floppy"]),
    "REL_DOWNLOAD": ("the download sold on Steam and GOG.com", ["download"]),
    "REL_ANY": ("either release", ["floppy", "download"]),
}

SOUND = {"NEED_ADLIB": "adlib", "NEED_SPEAKER": "speaker", "NEED_TANDY": "tandy", "NEED_ROLAND": "roland"}


# ---- what the controls and the system are called ----
# The frontend keeps no table of these: a core says what its own are called.
# MNEMONICS is the letter each button writes into a movie's text and heads its
# input column with, by the button's name - whole, or without its player ("P2
# Up" is found under "Up"), so one line serves every pad. AXIS_HEADERS is the
# short header of each axis's column. (An entry is read by position: a letter
# may change and no movie made before it is harmed.)
MNEMONICS = {
    "Up": "U", "Down": "D", "Left": "L", "Right": "R", "Up Left": "Q", "Up Right": "E",
    "Down Left": "Z", "Down Right": "C", "Enter": "N", "Space": "_", "Backspace": "B", "Esc": "X",
    "F1": "F", "F2": "G", "F3": "H", "Keypad +": "+", "Keypad -": "-", "Keypad *": "*",
    "Equals": "=", "0": "0", "1": "1", "2": "2", "3": "3", "4": "4", "5": "5", "6": "6", "7": "7",
    "8": "8", "9": "9", "A": "a", "B": "b", "C": "c", "D": "d", "E": "e", "F": "f", "G": "g",
    "H": "h", "I": "i", "J": "j", "K": "k", "L": "l", "M": "m", "N": "n", "O": "o", "P": "p",
    "Q": "q", "R": "r", "S": "s", "T": "t", "U": "u", "V": "v", "W": "w", "X": "x", "Y": "y",
    "Z": "z", "Shift": "^", "Sound": "V", "Graphics": "P",
}
SYSTEM_NAMES = {
    "SwordOfTheSamurai": "Sword of the Samurai",
}


def _bare(name):
    """A control's name without its player: "P2 Up" -> "Up"."""
    head, _, rest = name.partition(" ")
    return rest if rest and head[:1] == "P" and head[1:].isdigit() else name


def mnemonics_for(buttons):
    """The "mnemonics" of an input declaration: a letter for every one of its
    buttons, and for nothing else. A button nobody gave a letter stops the
    build - the engine would give it its rule's guess, and two columns of one
    pad would share a letter with nobody having decided it."""
    out = {}
    for b in buttons:
        key = b if b in MNEMONICS else _bare(b)
        if key not in MNEMONICS:
            raise SystemExit("no mnemonic for the button %r (MNEMONICS in %s)" % (b, __file__))
        out[key] = MNEMONICS[key]
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "waterbox.config")
    firmware = []
    for name, rel, need, size, sha1 in game_files():
        rel_text, rel_values = RELEASE[rel]
        if rom(name):
            desc = ("%s is not one of the game's files. It is %s. You have to supply it when the sound device is the"
                    " Roland, because this package includes none of it. Another MT-32 ROM that Munt knows can be used"
                    " in its place, and the project then records its hash.") % (name, WHAT[name])
        else:
            desc = ("%s, a file of Sword of the Samurai 445.03 (MicroProse, 1989, DOS), from %s. It holds %s. You"
                    " have to supply it, because this package includes none of the game's files. A modified file of"
                    " your own can be used in its place, and the project then records its hash.") % (
                name, rel_text, WHAT[name])
        if name == "START.EXE":
            desc += " The floppy and the download have different builds of this file. The Release setting says which one the wizard looks for, and both play the same game."
        if name == "ASOUND.SAM":
            desc += " It must be the download's file (dated 1-10-94), whichever release is played. OpenSamurai's AdLib sound is built from it, and the floppy's older driver is refused."
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
        "systemNames": SYSTEM_NAMES,
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
            "mnemonics": mnemonics_for(buttons()),
        },
        "settings": [
            {
                "name": "release",
                "display": "Release",
                "type": "enum",
                "options": ["floppy", "download"],
                "default": "floppy",
                "description": "Which release of Sword of the Samurai 445.03 the "
                    "project's files come from: the original floppy (the one"
                    " the published speedrun uses) or the download sold on "
                    "Steam and GOG.com. Both are the same game. Only "
                    "START.EXE (whose code OpenSamurai does not run) and the"
                    " AdLib driver differ.",
            },
            {
                "name": "sound",
                "display": "Sound",
                "type": "enum",
                "options": ["roland", "adlib", "speaker", "tandy", "none"],
                "default": "roland",
                "description": "The sound device the game is set up for: the Roland "
                    "MT-32 (the default), the AdLib, the IBM PC speaker, the"
                    " Tandy, or none. The AdLib needs the download's "
                    "ASOUND.SAM, because OpenSamurai's AdLib sound is built "
                    "from it and the floppy has an older driver. The Roland "
                    "needs RSOUND.SAM and the two ROMs of an MT-32 (version "
                    "1.07, the first generation, which the game was made "
                    "for). The project supplies them as firmware. With the "
                    "PC speaker the title sequence runs slower, as it did "
                    "originally.",
            },
            {
                "name": "skip_title",
                "display": "Skip the Title",
                "type": "bool",
                "default": False,
                "description": "Starts the game with its /NT option. The title sequence"
                    " is left out and the game starts at the copy-protection"
                    " question.",
            },
            {
                "name": "random_seed",
                "display": "Random Seed",
                "type": "string",
                "default": "0",
                "description": "The starting number for all of the game's random "
                    "numbers: the crest question, the characters, the "
                    "encounters, and the opponents in duels, melees and "
                    "battles. The DOS programs took their random numbers "
                    "from the clock. OpenSamurai takes all of them from this"
                    " one number. It can be any 64-bit number, in decimal or"
                    " in hexadecimal after 0x, in the same form "
                    "OpenSamurai's own program takes it (OPENSAMURAI_SEED). "
                    "A number that program printed gives the same random "
                    "numbers here. A movie records the number it ran with.",
            },
            {
                "name": "clock_start",
                "display": "Start Date and Time",
                "type": "string",
                "default": "1989-10-25 12:00:00",
                "description": "The PC's date and time when the game starts, written as"
                    " YYYY-MM-DD HH:MM:SS. The clock then advances with the "
                    "frames. The game reads it as the time, for example for "
                    "how long a prompt waits and when an encounter on the "
                    "road can happen. A movie records the start time it ran "
                    "with.",
            },
        ],
        "firmware": firmware,
    }
    with open(out, "w") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
