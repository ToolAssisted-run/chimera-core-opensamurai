/* game-state.c - Sword of the Samurai's memory and properties (chimera
 * docs/game-cores.md).
 *
 * OpenSamurai keeps the DOS machine's memory as the programs had it: the
 * megabyte a segment addresses (every program's data segment, its stack, the
 * buffers it allocated - all at the segments they had in the real game), and
 * the 1 KB block the launcher shares between the programs. Those are domains
 * in place, read and written where the game has them, so a watch on an
 * address found in the original game's memory is a watch on the same thing
 * here. The Game State block holds what is the core's: the frames shown; and
 * Saved Games is the file the game keeps its saves in, as it last wrote it.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "samurai-driver.h"

#include "asm2c.h"
#include "shared.h"

/* ------------------------------------------------------ the Game State block */

#pragma pack(push, 1)
typedef struct
{
	uint32_t frames;   /* +0 video frames shown (70.086 a second) */
	uint8_t ended;     /* +4 the player quit the game */
} game_state;
#pragma pack(pop)

static game_state g_state;

void gamestate_from_game(void)
{
	g_state.frames = (uint32_t)samdrv_frames();
	g_state.ended = (uint8_t)samdrv_exited();
}

/* nothing in the block is the game's to be told */
void gamestate_to_game(void) {}

/* ------------------------------------------------------------------ domains */

typedef struct
{
	const char *name;
	uint8_t *ptr;
	int64_t size;
	int writable;
} domain;

static domain g_domains[5];
static int g_ndomains;

/* The megabyte's first byte. far_ptr sends two segments elsewhere - the
 * running program's data segment to its image and the shared block's to the
 * block - and before the launcher has run both are 0, so far_ptr(0, 0) is the
 * shared block then. Segment 1000 is neither, ever (the block is at 1942, the
 * programs' data segments above 2D00), and far_ptr gives it as it is. */
static uint8_t *megabyte(void) { return far_ptr(0x1000, 0) - 0x10000; }

static void domains_init(void)
{
	if (g_ndomains) return;
	g_domains[g_ndomains++] = (domain){ "Game State", (uint8_t *)&g_state, sizeof g_state, 0 };
	/* the 640 KB DOS gives its programs, every program's data segment among them */
	g_domains[g_ndomains++] = (domain){ "Conventional Memory", megabyte(), 0xA0000, 1 };
	g_domains[g_ndomains++] = (domain){ "Shared Block", shared.b, SHARED_SIZE, 1 };
	/* the VGA's memory at A000 (mode 13h: the picture, a byte a pixel) */
	g_domains[g_ndomains++] = (domain){ "Video Memory", megabyte() + 0xA0000, 0x10000, 1 };
	/* the game's saved games (TALLTALE.DAT) as it last wrote them: the blank
	 * file of a new installation, or the project's own (samurai-driver.c) */
	const uint8_t *saved;
	long len;
	if (files_saved("TALLTALE.DAT", &saved, &len))
		g_domains[g_ndomains++] = (domain){ "Saved Games", (uint8_t *)saved, 7650, 0 };
}

int samdrv_domain_count(void)
{
	domains_init();
	return g_ndomains;
}

const char *samdrv_domain_name(int i)
{
	domains_init();
	return i >= 0 && i < g_ndomains ? g_domains[i].name : "";
}

uint8_t *samdrv_domain_ptr(int i)
{
	domains_init();
	return i >= 0 && i < g_ndomains ? g_domains[i].ptr : NULL;
}

int64_t samdrv_domain_size(int i)
{
	domains_init();
	return i >= 0 && i < g_ndomains ? g_domains[i].size : 0;
}

int samdrv_domain_writable(int i)
{
	domains_init();
	return i >= 0 && i < g_ndomains && g_domains[i].writable;
}

/* -------------------------------------------------------------------- table */

static char g_table[64 * 1024];
static int g_len;

static void add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void add(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(g_table + g_len, sizeof g_table - (size_t)g_len, fmt, ap);
	va_end(ap);
	if (n > 0) g_len += n;
}

/* one property: name, domain, offset, type, group, extra JSON members (or "") */
static int g_first = 1;
static void prop(const char *name, const char *dom, long off, const char *type, const char *group, const char *extra)
{
	add("%s\n    { \"name\": \"%s\", \"domain\": \"%s\", \"offset\": %ld, \"type\": \"%s\", \"group\": \"%s\"%s%s }",
		g_first ? "" : ",", name, dom, off, type, group, extra[0] ? ", " : "", extra);
	g_first = 0;
}

static void table_init(void)
{
	if (g_len) return;
	add("{ \"properties\": [");

	prop("Frame", "Game State", 0, "u32", "Game", "\"writable\": false, \"description\": \"Video frames shown, 70.086 a second\"");
	prop("Ended", "Game State", 4, "bool", "Game", "\"writable\": false, \"description\": \"The player quit the game\"");

	/* the shared block: what the launcher and the programs hand each other
	 * (OpenSamurai's source/shared.h and docs/FINDINGS.md) */
	prop("Samurai.Name", "Shared Block", 0x2D4, "string", "Samurai", "\"length\": 21, \"description\": \"The player's name, as character creation took it\"");
	prop("Options.Video Mode", "Shared Block", 0x22, "u16", "Options",
		"\"writable\": false, \"values\": { \"0\": \"CGA\", \"1\": \"Tandy\", \"2\": \"EGA\", \"3\": \"MCGA\", \"4\": \"VGA\", \"5\": \"Hercules\" }");
	prop("Options.Sound Mode", "Shared Block", 0x310, "u16", "Options",
		"\"values\": { \"0\": \"Music and effects\", \"1\": \"Effects\", \"2\": \"Silent\" }, \"description\": \"Alt+V cycles it\"");
	prop("Duel.Background", "Shared Block", 0x5C, "u16", "Duel", "\"values\": { \"0\": \"Village\", \"1\": \"Outside\", \"2\": \"Inside\" }");
	prop("Duel.Player Won", "Shared Block", 0x4C, "u16", "Duel", "");
	prop("Duel.Player Fell", "Shared Block", 0x4E, "u16", "Duel", "");
	prop("Duel.Retreated", "Shared Block", 0x62, "u16", "Duel", "");
	prop("Samurai.Wounded", "Shared Block", 0x54, "u16", "Samurai", "\"description\": \"The player carries a wound (a melee into a duel, and back to the role-playing game)\"");

	add("\n  ]\n}\n");
}

const char *samdrv_game_properties(void)
{
	table_init();
	return g_table;
}
