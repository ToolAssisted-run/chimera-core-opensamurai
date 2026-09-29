/* samurai-driver.c - Sword of the Samurai (OpenSamurai) as a machine stepped
 * one video frame at a time.
 *
 * OpenSamurai's game_run() is the DOS game's launcher and all its programs as
 * one blocking call: it asks its host for the time, waits for the next video
 * frame and hands each finished frame to the host (source/game.h). Here it
 * runs on a stack of its own (coro.c) and its host is this file:
 *
 * - the time is a virtual clock, OpenSamurai's own test clock (tests/
 *   gametest.c): each look at it is 20 microseconds later, and a wait jumps to
 *   its end. Nothing reads the host's real clock, so a run is the same run
 *   every time. The melee is the exception that proves the rule: it counts
 *   its passes against its tick counter, so a read of that counter costs what
 *   one pass cost on the machine OpenSamurai's melee was checked against
 *   (GameHost.meleeTickRead, host_melee_tick_read below);
 * - a finished frame ends the step: the game's stack is left where it is and
 *   the core returns to the frontend. The frame's sound comes right after its
 *   picture, so the step ends after that, or at the game's next look at its
 *   host if a frame has no sound;
 * - the buttons of the next step reach the game when it continues: each key
 *   that went down is typed into the BIOS's buffer and its make code goes to
 *   the programs that read the keyboard themselves; a key let go gives its
 *   break code. A key held repeats as the AT keyboard's does after a reset
 *   (half a second, then 10.9 a second), on the virtual clock - the menus and
 *   the battle's cursor move a step a key.
 *
 * The random seed is a setting (OpenSamurai draws every program's seed from
 * it, where the DOS programs took the clock's), and so is the date and time
 * the machine starts at, which the game reads as the PC's clock: a movie
 * records both.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "coro.h"
#include "samurai-driver.h"
#include "sha1.h"

#include "asm2c.h"
#include "game.h"
#include "vga.h"

/* the MT-32: Munt's libmt32emu, through its C API */
#define MT32EMU_API_TYPE 1
#include <mt32emu.h>

int files_put(const char *name, const uint8_t *data, long len);   /* files.c */

/* ------------------------------------------------------------ the game's files */

/* Sword of the Samurai 445.03 (MicroProse, 1989) is the release OpenSamurai is
 * rebuilt from: the original floppy's files. The download sold on Steam and
 * GOG.com is the same but for START.EXE (whose code the reconstruction does
 * not run) and a later AdLib driver - the one OpenSamurai's AdLib is rebuilt
 * from, and so the one the AdLib sound needs whichever release is played. The
 * hashes are the originals', which the declarations give the frontend to find
 * them by; a file of the project's own is taken in their place (check_files). */
enum { REL_FLOPPY = 1, REL_DOWNLOAD = 2, REL_ANY = 3 };
enum { NEED_ALWAYS, NEED_ADLIB, NEED_SPEAKER, NEED_TANDY, NEED_ROLAND };

static const struct
{
	const char *name;
	int releases;
	int need;
	long size;
	const char *sha1;
} k_files[] = {
	{ "MISC.EXE", REL_ANY, NEED_ALWAYS, 980, "1726908A82181E5B597DE74BF01888419D167732" },
	{ "NSOUND.SAM", REL_ANY, NEED_ALWAYS, 672, "1FFF0165DB22B79ACFFB3CFD5A310E546F045E40" },
	{ "MGRAPHIC.EXE", REL_ANY, NEED_ALWAYS, 5036, "D443AA8ACD355E84D8F092AA287712F4BEB812B9" },
	{ "FONTS.SAM", REL_ANY, NEED_ALWAYS, 6415, "7476FC5AAF4AF33C3248FC7D4C510FE3A36305EE" },
	{ "START.EXE", REL_FLOPPY, NEED_ALWAYS, 39959, "B83871678B5A8482E05444541B9D955AD94F88E8" },
	{ "START.EXE", REL_DOWNLOAD, NEED_ALWAYS, 39959, "1A4BF27C3CD483587BA6D02D849445FE175C180B" },
	{ "RP.EXE", REL_ANY, NEED_ALWAYS, 125143, "A105E7F16A0F83FD1BD3C6050D3BCB171A6C3FA0" },
	{ "DUEL.EXE", REL_ANY, NEED_ALWAYS, 35995, "605207B73BDB3AE29E4A9215761CD329403F4709" },
	{ "BATTLE.EXE", REL_ANY, NEED_ALWAYS, 47335, "9DE05BC7FDDABD21C0A983F9D80E5DFE9C4B0905" },
	{ "MELEE.EXE", REL_ANY, NEED_ALWAYS, 80779, "0726F424DDF7168BD41C8A808E8C55A3B305ADA7" },
	{ "START.CAT", REL_ANY, NEED_ALWAYS, 87653, "E2DF14CB62798679FF6F61511E7A57D836295F6F" },
	{ "RP.CAT", REL_ANY, NEED_ALWAYS, 351861, "0D07C11758A687CA08EF5E98D7747A8BB76DBAD7" },
	{ "DUEL.CAT", REL_ANY, NEED_ALWAYS, 37557, "1AEEF1FFE8357479C5F0A7CFC5C986E613BBFFAA" },
	{ "MELEE.CAT", REL_ANY, NEED_ALWAYS, 21725, "8A0C7AA50140E3FC4200478519BCC700C5E09D78" },
	{ "EGRAPHIC.MEL", REL_ANY, NEED_ALWAYS, 7994, "F80B114750346882459DEA826EB93E66DB5E9B16" },
	{ "ICONS.PIC", REL_ANY, NEED_ALWAYS, 6351, "7134F9CBCCA3266636FD4D2FA881A88BDC52350A" },
	{ "ASOUND.SAM", REL_ANY, NEED_ADLIB, 12420, "A59B06B89C7DCB4526C985785A71DBA33DA58CBC" },
	{ "ISOUND.SAM", REL_ANY, NEED_SPEAKER, 4622, "3F89D7FE15A8BE29CC741F97FCCAD4248BD119C6" },
	{ "TSOUND.SAM", REL_ANY, NEED_TANDY, 9263, "36E116015977DA18BD61A33081DC418A98A4E516" },
	{ "RSOUND.SAM", REL_ANY, NEED_ROLAND, 14699, "30D652D33D2D6902CB402F079F57F1B874AE8629" },
	/* the Roland's own: an MT-32 of the first generation, the sound the game
	 * was made for (v1.07; the DOSBox-X core's firmware ids and hashes) */
	{ "MT32_CONTROL.ROM", REL_ANY, NEED_ROLAND, 65536, "B083518FFFB7F66B03C23B7EB4F868E62DC5A987" },
	{ "MT32_PCM.ROM", REL_ANY, NEED_ROLAND, 524288, "F6B1EEBC4B2D200EC6D3D21D51325D5B48C60252" },
};
#define SAM_FILE_COUNT ((int)(sizeof k_files / sizeof k_files[0]))

/* the floppy's AdLib driver: an older build than the one OpenSamurai's AdLib is */
#define FLOPPY_ASOUND_SHA1 "BA88184505AC6810C004F41246C2A6AE024703D6"

/* ------------------------------------------------------------------ the state */

static struct
{
	coro *co;
	GameHost host;
	int release;
	char sound;               /* the setup's driver letter: 'A', 'I', 'T', 'R', or 0 for none */
	int presented;            /* a frame was shown: the step ends at the next chance */
	int exited, exit_code;
	uint64_t vnow;            /* the virtual clock, microseconds */
	uint64_t read_cost;       /* what the next look at the clock costs, if not the usual */
	uint64_t frames;          /* video frames shown */
	/* the buttons */
	uint8_t held[SAM_BTN_COUNT];
	uint8_t want[SAM_BTN_COUNT];
	int shift_down, alt_down;
	int repeat_btn;           /* the key the keyboard repeats (the last one down), or -1 */
	uint64_t repeat_next;     /* when it repeats next, on the virtual clock */
	/* the BIOS's keyboard buffer, as the host holds it */
	uint16_t keys[64];
	int key_head, key_tail;
	int read;                 /* the program looked at its keyboard in this step */
	/* what the step hands out */
	int render;
	int audio_n;
	int16_t audio[2 * SAM_AUDIO_MAX_SAMPLES];
	uint32_t video[SAM_VIDEO_WIDTH * SAM_VIDEO_HEIGHT];
} g;

/* the MT-32, when the sound is the Roland's (below) */
static mt32emu_context g_mt32;

/* the names the game finds in its directory: the files the project mounted */
static const char *g_known[SAM_FILE_COUNT];

/* --------------------------------------------------------------------- input */

/* What each button is on the PC's keyboard: the scan code (set 1) and the key
 * the BIOS gives for it (scan << 8 | ASCII; a letter's capital while Shift is
 * down). The directions are the numeric keypad's with Num Lock off; the
 * commands are Alt with a letter, whose BIOS key has no ASCII. */
enum { MOD_ALT = 1 };
static const struct { uint8_t scan; uint16_t bios; uint8_t mods; } k_keys[SAM_BTN_COUNT] = {
	[SAM_BTN_UP] = { 0x48, 0x4800, 0 }, [SAM_BTN_DOWN] = { 0x50, 0x5000, 0 },
	[SAM_BTN_LEFT] = { 0x4B, 0x4B00, 0 }, [SAM_BTN_RIGHT] = { 0x4D, 0x4D00, 0 },
	[SAM_BTN_UP_LEFT] = { 0x47, 0x4700, 0 }, [SAM_BTN_UP_RIGHT] = { 0x49, 0x4900, 0 },
	[SAM_BTN_DOWN_LEFT] = { 0x4F, 0x4F00, 0 }, [SAM_BTN_DOWN_RIGHT] = { 0x51, 0x5100, 0 },
	[SAM_BTN_ENTER] = { 0x1C, 0x1C0D, 0 }, [SAM_BTN_SPACE] = { 0x39, 0x3920, 0 },
	[SAM_BTN_BACKSPACE] = { 0x0E, 0x0E08, 0 }, [SAM_BTN_ESC] = { 0x01, 0x011B, 0 },
	[SAM_BTN_F1] = { 0x3B, 0x3B00, 0 }, [SAM_BTN_F2] = { 0x3C, 0x3C00, 0 }, [SAM_BTN_F3] = { 0x3D, 0x3D00, 0 },
	[SAM_BTN_PLUS] = { 0x4E, 0x4E2B, 0 }, [SAM_BTN_MINUS] = { 0x4A, 0x4A2D, 0 },
	[SAM_BTN_STAR] = { 0x37, 0x372A, 0 }, [SAM_BTN_EQUALS] = { 0x0D, 0x0D3D, 0 },
	[SAM_BTN_0] = { 0x0B, 0x0B30, 0 }, [SAM_BTN_1] = { 0x02, 0x0231, 0 }, [SAM_BTN_2] = { 0x03, 0x0332, 0 },
	[SAM_BTN_3] = { 0x04, 0x0433, 0 }, [SAM_BTN_4] = { 0x05, 0x0534, 0 }, [SAM_BTN_5] = { 0x06, 0x0635, 0 },
	[SAM_BTN_6] = { 0x07, 0x0736, 0 }, [SAM_BTN_7] = { 0x08, 0x0837, 0 }, [SAM_BTN_8] = { 0x09, 0x0938, 0 },
	[SAM_BTN_9] = { 0x0A, 0x0A39, 0 },
	[SAM_BTN_A] = { 0x1E, 0x1E61, 0 }, [SAM_BTN_B] = { 0x30, 0x3062, 0 }, [SAM_BTN_C] = { 0x2E, 0x2E63, 0 },
	[SAM_BTN_D] = { 0x20, 0x2064, 0 }, [SAM_BTN_E] = { 0x12, 0x1265, 0 }, [SAM_BTN_F] = { 0x21, 0x2166, 0 },
	[SAM_BTN_G] = { 0x22, 0x2267, 0 }, [SAM_BTN_H] = { 0x23, 0x2368, 0 }, [SAM_BTN_I] = { 0x17, 0x1769, 0 },
	[SAM_BTN_J] = { 0x24, 0x246A, 0 }, [SAM_BTN_K] = { 0x25, 0x256B, 0 }, [SAM_BTN_L] = { 0x26, 0x266C, 0 },
	[SAM_BTN_M] = { 0x32, 0x326D, 0 }, [SAM_BTN_N] = { 0x31, 0x316E, 0 }, [SAM_BTN_O] = { 0x18, 0x186F, 0 },
	[SAM_BTN_P] = { 0x19, 0x1970, 0 }, [SAM_BTN_Q] = { 0x10, 0x1071, 0 }, [SAM_BTN_R] = { 0x13, 0x1372, 0 },
	[SAM_BTN_S] = { 0x1F, 0x1F73, 0 }, [SAM_BTN_T] = { 0x14, 0x1474, 0 }, [SAM_BTN_U] = { 0x16, 0x1675, 0 },
	[SAM_BTN_V] = { 0x2F, 0x2F76, 0 }, [SAM_BTN_W] = { 0x11, 0x1177, 0 }, [SAM_BTN_X] = { 0x2D, 0x2D78, 0 },
	[SAM_BTN_Y] = { 0x15, 0x1579, 0 }, [SAM_BTN_Z] = { 0x2C, 0x2C7A, 0 },
	[SAM_BTN_SHIFT] = { 0x2A, 0, 0 },
	[SAM_BTN_SOUND] = { 0x2F, 0x2F00, MOD_ALT },
	[SAM_BTN_GRAPHICS] = { 0x2C, 0x2C00, MOD_ALT },
};

void samdrv_set_button(int index, int down)
{
	if (index >= 0 && index < SAM_BTN_COUNT) g.want[index] = down ? 1 : 0;
}

static void type_key(uint16_t k)
{
	if (!k || (g.key_tail + 1) % 64 == g.key_head) return;   /* (the buffer full: the key is lost, as the BIOS's) */
	g.keys[g.key_tail] = k;
	g.key_tail = (g.key_tail + 1) % 64;
}

/* whether a held button still wants Alt down */
static int alt_held(void)
{
	for (int b = 0; b < SAM_BTN_COUNT; b++)
		if (g.held[b] && (k_keys[b].mods & MOD_ALT)) return 1;
	return 0;
}

/* whether another held button holds the same key (V and Sound are both V) */
static int key_held_by_other(int self)
{
	for (int b = 0; b < SAM_BTN_COUNT; b++)
		if (b != self && g.held[b] && k_keys[b].scan == k_keys[self].scan) return 1;
	return 0;
}

/* the AT keyboard's typematic defaults: the delay and the time between repeats */
#define REPEAT_DELAY_US 500000
#define REPEAT_EVERY_US 91743   /* 10.9 a second */

/* a key going down, or repeating: its make code (with Alt's, for a command,
 * already down), and the key it types */
static void key_press(int b)
{
	if (!key_held_by_other(b) || g.repeat_btn == b) game_key(k_keys[b].scan, false, true);
	uint16_t k = k_keys[b].bios;
	const uint8_t ascii = (uint8_t)k;
	if (!(k_keys[b].mods & MOD_ALT) && g.shift_down && ascii >= 'a' && ascii <= 'z') k = (uint16_t)(k - ('a' - 'A'));
	type_key(k);
}

/* The buttons that changed, as the keyboard would have given them: Shift
 * first, then the rest in the panel's order. A command's Alt is a real key,
 * put down before its letter and lifted after unless another held command
 * still holds it. Runs on the game's stack, where the frontend's keys arrive
 * in OpenSamurai's own frontend: in the middle of the game's look at its host. */
static void apply_buttons(void)
{
	for (int pass = 0; pass < 2; pass++)
	{
		for (int b = 0; b < SAM_BTN_COUNT; b++)
		{
			if ((b == SAM_BTN_SHIFT) != (pass == 0) || g.want[b] == g.held[b]) continue;
			const int down = g.want[b];
			g.held[b] = g.want[b];
			if (b == SAM_BTN_SHIFT)
			{
				g.shift_down = down;
				game_key(0x2A, false, down);
				continue;
			}
			const int alt = k_keys[b].mods & MOD_ALT;
			if (down)
			{
				if (alt && !g.alt_down) { g.alt_down = 1; game_key(0x38, false, true); }
				key_press(b);
				/* the keyboard repeats the last key that went down */
				g.repeat_btn = b;
				g.repeat_next = g.vnow + REPEAT_DELAY_US;
			}
			else
			{
				if (!key_held_by_other(b)) game_key(k_keys[b].scan, false, false);
				if (g.alt_down && !alt_held()) { g.alt_down = 0; game_key(0x38, false, false); }
				if (g.repeat_btn == b) g.repeat_btn = -1;
			}
		}
	}
	/* a key held long enough repeats, each time its make code again and the
	 * key it types; a step is a frame, so the repeats that fell in it come
	 * together at its start */
	while (g.repeat_btn >= 0 && g.held[g.repeat_btn] && g.vnow >= g.repeat_next)
	{
		key_press(g.repeat_btn);
		g.repeat_next += REPEAT_EVERY_US;
	}
}

/* ------------------------------------------------------------------ the host */

/* The step ends here, at the first look at the host after a frame was shown:
 * the game's stack waits, and when the next step comes the buttons arrive
 * before the game goes on. */
static void settle(void)
{
	if (!g.presented) return;
	g.presented = 0;
	coro_yield(g.co);
	apply_buttons();
}

static void convert_frame(void)
{
	static uint8_t planar[SAM_VIDEO_WIDTH * SAM_VIDEO_HEIGHT];
	const uint8_t *vram = far_ptr(0xA000, 0);
	if (vga.planar)
	{
		vga_render(planar, asm_dac);
		vram = planar;
	}
	for (int i = 0; i < SAM_VIDEO_WIDTH * SAM_VIDEO_HEIGHT; i++)
	{
		const uint8_t *c = asm_dac[vram[i]];
		const uint32_t r = (uint32_t)((c[0] & 63) << 2 | (c[0] & 63) >> 4);
		const uint32_t gr = (uint32_t)((c[1] & 63) << 2 | (c[1] & 63) >> 4);
		const uint32_t b = (uint32_t)((c[2] & 63) << 2 | (c[2] & 63) >> 4);
		g.video[i] = 0xFF000000u | r << 16 | gr << 8 | b;
	}
}

static void host_present(void *ctx)
{
	(void)ctx;
	settle();   /* (a frame with no look at the host since the last) */
	g.frames++;
	if (g.render) convert_frame();
	g.presented = 1;
}

/* BATTLE waits for its next step (every 17 frames) in a loop that looks at
 * the time and the keyboard and redraws its cursor on every pass, about 37
 * calls of the graphics driver a look: at 20 microseconds a look that is 320
 * passes a frame, and the battle ran slower than real time. Its steps come
 * with the frames, whatever a look costs (OpenSamurai's author), so a look
 * while it runs costs 200 microseconds, 70 passes a frame: seven times the
 * speed, and the same machine - measured, every byte of memory and the
 * picture equal at 20 and at 200 after a skirmish's first 1,400 frames. (At
 * 1,000 one byte of the battle's stack, DS:8238, is left otherwise.) */
#define BATTLE_DS 0x3109
#define BATTLE_READ_US 200

static uint64_t host_now(void *ctx)
{
	(void)ctx;
	settle();
	const uint64_t cost = g.read_cost ? g.read_cost : g_dsSeg == BATTLE_DS ? BATTLE_READ_US : 20;
	g.read_cost = 0;
	return g.vnow += cost;
}

/* GameHost.meleeTickRead: MELEE is about to read its tick counter, and look
 * at the time to do it. It counts passes against the ticks in two places, and
 * what a read costs there decides what machine it thinks it is on (the values
 * are OpenSamurai's own virtual clocks', its author's, from the oracle's
 * DOSBox-X runs):
 * - the start-up speed test (072B) counts empty loops until 15 ticks and
 *   wants 15,000 or more, else it plays the slow machine's melee (coarser
 *   steps, fewer enemies; DS:342A = 1); at 10 microseconds a read it counts
 *   about 21,400, the fast machine the oracle was (DS:342A = 0). Not 0: the
 *   loop would never see a tick;
 * - the main loop (3BC2) reads it once a pass and the reinforcements' countdown
 *   counts passes: DOSBox-X ran about 21 passes a 60 Hz tick. A pass also
 *   makes a few ordinary looks (the keyboard's), so the read itself costs
 *   690 microseconds, 21.1 passes a tick, where 20 would bring the
 *   reinforcements 30 times sooner. */
static void host_melee_tick_read(void *ctx, int site)
{
	(void)ctx;
	g.read_cost = site == 0x072B ? 10 : site == 0x3BC2 ? 690 : 0;
}

static void host_sleep_until(void *ctx, uint64_t t)
{
	(void)ctx;
	settle();
	if (t > g.vnow) g.vnow = t;
}

static int host_key_waiting(void *ctx)
{
	(void)ctx;
	settle();
	g.read = 1;
	return g.key_head != g.key_tail;
}

static uint16_t host_read_key(void *ctx)
{
	(void)ctx;
	settle();
	g.read = 1;
	if (g.key_head == g.key_tail) return 0;
	const uint16_t k = g.keys[g.key_head];
	g.key_head = (g.key_head + 1) % 64;
	return k;
}

/* The frame's sound, which comes right after its picture: then the step ends.
 * The chips' sound is mono, the same on both sides; the MT-32's is stereo and
 * is added to it, as the frontend mixes them. */
static void host_audio(void *ctx, const int16_t *samples, int n)
{
	(void)ctx;
	if (g.audio_n + n > SAM_AUDIO_MAX_SAMPLES) n = SAM_AUDIO_MAX_SAMPLES - g.audio_n;
	if (n > 0)
	{
		int16_t *out = g.audio + 2 * g.audio_n;
		for (int k = 0; k < n; k++) out[2 * k] = out[2 * k + 1] = samples[k];
		if (g_mt32)
		{
			static int16_t mt[2 * SAM_AUDIO_MAX_SAMPLES];
			mt32emu_render_bit16s(g_mt32, mt, (mt32emu_bit32u)n);
			for (int k = 0; k < 2 * n; k++)
			{
				const int v = out[k] + mt[k];
				out[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
			}
		}
		g.audio_n += n;
	}
	settle();
}

/* ------------------------------------------------------------------ the MT-32 */

/* The Roland (RSOUND.SAM) drives the MPU-401, whose bytes OpenSamurai hands the
 * host with their time in the sound's frames since the game began; the MT-32
 * takes them at that time and plays them into the frames the step renders -
 * as OpenSamurai's own frontend does. */
static mt32emu_report_handler_version MT32EMU_C_CALL mt32_version(mt32emu_report_handler_i i)
{
	(void)i;
	return MT32EMU_REPORT_HANDLER_VERSION_0;
}
/* what Munt would print (its LCD's messages, its debug reports): to nobody -
 * the guest's console is the frontend's */
static void MT32EMU_C_CALL mt32_debug(void *instance, const char *fmt, va_list list) { (void)instance; (void)fmt; (void)list; }
static void MT32EMU_C_CALL mt32_lcd(void *instance, const char *message) { (void)instance; (void)message; }
static const mt32emu_report_handler_i_v0 k_mt32_reports = { .getVersionID = mt32_version, .printDebug = mt32_debug, .showLCDMessage = mt32_lcd };

static long read_all(const char *name, uint8_t **data)
{
	FILE *f = fopen(name, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	*data = n > 0 ? malloc((size_t)n) : NULL;
	const long got = *data ? (long)fread(*data, 1, (size_t)n, f) : -1;
	fclose(f);
	return got == n ? n : -1;
}

static int mt32_open(char *err, int errsize)
{
	mt32emu_report_handler_i reports = { &k_mt32_reports };
	g_mt32 = mt32emu_create_context(reports, NULL);
	static const char *const roms[] = { "MT32_CONTROL.ROM", "MT32_PCM.ROM" };
	for (int i = 0; i < 2; i++)
	{
		uint8_t *data;
		const long n = read_all(roms[i], &data);
		/* Munt keeps the data as given, it does not copy it: the buffers live
		 * as long as the machine */
		if (n < 0 || mt32emu_add_rom_data(g_mt32, data, (size_t)n, NULL) < 0)
		{
			snprintf(err, (size_t)errsize, "the MT-32 did not take %s", roms[i]);
			return 0;
		}
	}
	mt32emu_set_stereo_output_samplerate(g_mt32, SAM_AUDIO_RATE);
	if (mt32emu_open_synth(g_mt32) != MT32EMU_RC_OK)
	{
		snprintf(err, (size_t)errsize, "the MT-32 could not start");
		return 0;
	}
	return 1;
}

static void host_midi(void *ctx, uint8_t byte, uint64_t sample)
{
	(void)ctx;
	settle();
	if (g_mt32)
		mt32emu_parse_stream_at(g_mt32, &byte, 1, mt32emu_convert_output_to_synth_timestamp(g_mt32, (mt32emu_bit32u)sample));
}

static void game_body(void)
{
	g.exit_code = game_run(&g.host);
	g.exited = 1;
}

/* ------------------------------------------------------------------ settings */

/* the start: "YYYY-MM-DD HH:MM:SS", or 0 */
static int parse_clock(const char *s, GameClock *c)
{
	int y, mo, d, h, mi, se;
	char tail;
	if (sscanf(s, "%d-%d-%d %d:%d:%d%c", &y, &mo, &d, &h, &mi, &se, &tail) != 6) return 0;
	if (y < 1980 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 59)
		return 0;
	*c = (GameClock) { y, mo, d, h, mi, se, 0 };
	return 1;
}

static int file_needed(int i)
{
	if (!(k_files[i].releases & g.release)) return 0;
	switch (k_files[i].need)
	{
	case NEED_ADLIB: return g.sound == 'A';
	case NEED_SPEAKER: return g.sound == 'I';
	case NEED_TANDY: return g.sound == 'T';
	case NEED_ROLAND: return g.sound == 'R';
	default: return 1;
	}
}

/* Every file the settings call for is there. What is in it is the project's:
 * a file of its own (a modified one, the other release's START.EXE, another
 * dump) is taken as it is, and Chimera pins ITS hash in the project
 * (user-decided, 2026-09-29: a game core's firmware may be custom);
 * OpenSamurai itself only warns about a version it was not rebuilt from. The
 * one refusal left is a build known not to work: the original floppy's AdLib
 * driver, older than the one OpenSamurai's AdLib is rebuilt from. */
static int check_files(char *err, int errsize)
{
	int known = 0;
	for (int i = 0; i < SAM_FILE_COUNT; i++)
	{
		if (!file_needed(i)) continue;
		FILE *fp = fopen(k_files[i].name, "rb");
		if (!fp)
		{
			snprintf(err, (size_t)errsize, "Sword of the Samurai needs %s - add it as the project's firmware.", k_files[i].name);
			return 0;
		}
		if (!strcmp(k_files[i].name, "ASOUND.SAM"))
		{
			char hex[41];
			long size = 0;
			const int ok = sha1_file(fp, hex, &size);
			if (!ok)
			{
				fclose(fp);
				snprintf(err, (size_t)errsize, "%s could not be read.", k_files[i].name);
				return 0;
			}
			if (!strcmp(hex, FLOPPY_ASOUND_SHA1))
			{
				fclose(fp);
				snprintf(err, (size_t)errsize,
					"This ASOUND.SAM is the original floppy's AdLib driver, an older build than the one OpenSamurai's "
					"AdLib is rebuilt from (the download's, %ld bytes, dated 1-10-94). Add that one, or choose "
					"another sound.", k_files[i].size);
				return 0;
			}
		}
		fclose(fp);
		g_known[known++] = k_files[i].name;
	}
	files_set_known(g_known, known);
	return 1;
}

/* The game's saved games: TALLTALE.DAT, six scrolls of 1,275 bytes. The disk
 * brings it blank - 7,650 zeros, as the original floppy's is - and the game
 * quits when Restore finds no file ("rp: talltale.dat"), so a project without
 * one starts with the blank file, as a new installation did. A project may
 * bring its own in the "savedgame" slot: it is put where the game keeps its
 * saved games, so that Restore finds them, and the game's saves go there too,
 * in memory, never over the project's file. */
#define TALLTALE_SIZE 7650

static int load_saved_game(char *err, int errsize)
{
	char path[256];
	if (!wbx_slot_first("savedgame", path, sizeof path))
	{
		static const uint8_t blank[TALLTALE_SIZE];
		return files_put("TALLTALE.DAT", blank, TALLTALE_SIZE);
	}
	FILE *f = fopen(path, "rb");
	static uint8_t buf[65536];
	const long n = f ? (long)fread(buf, 1, sizeof buf, f) : -1;
	if (f) fclose(f);
	if (n <= 0 || !files_put("TALLTALE.DAT", buf, n))
	{
		snprintf(err, (size_t)errsize, "the project's saved game %s could not be read", path);
		return 0;
	}
	return 1;
}

int samdrv_init(char *err, int errsize)
{
	memset(&g, 0, sizeof g);
	g.repeat_btn = -1;

	char text[64];
	if (wbx_setting_str("release", text, sizeof text) < 0) strcpy(text, "floppy");
	if (!strcmp(text, "floppy")) g.release = REL_FLOPPY;
	else if (!strcmp(text, "download")) g.release = REL_DOWNLOAD;
	else
	{
		snprintf(err, (size_t)errsize, "the release setting is %s; it is floppy or download", text);
		return 0;
	}

	/* the Roland MT-32 unless the project says otherwise (user-decided,
	 * 2026-09-29: game cores default to the MT-32) */
	if (wbx_setting_str("sound", text, sizeof text) < 0) strcpy(text, "roland");
	if (!strcmp(text, "adlib")) g.sound = 'A';
	else if (!strcmp(text, "speaker")) g.sound = 'I';
	else if (!strcmp(text, "tandy")) g.sound = 'T';
	else if (!strcmp(text, "roland")) g.sound = 'R';
	else if (!strcmp(text, "none")) g.sound = 0;
	else
	{
		snprintf(err, (size_t)errsize, "the sound setting is %s; it is adlib, speaker, tandy, roland or none", text);
		return 0;
	}

	/* the seed: any 64-bit number, decimal or 0x hexadecimal, as OpenSamurai's
	 * own frontend takes it (OPENSAMURAI_SEED, strtoull) - so a seed it printed
	 * draws the same random numbers here */
	uint64_t seed = 0;
	if (wbx_setting_str("random_seed", text, sizeof text) >= 0)
	{
		char *end = NULL;
		const char *p = text;
		while (*p == ' ') p++;
		errno = 0;
		seed = strtoull(p, &end, 0);
		while (end && *end == ' ') end++;
		if (!*p || *p == '-' || errno || !end || *end)
		{
			snprintf(err, (size_t)errsize, "the random seed is \"%s\"; it is a number from 0 to 18446744073709551615, or 0x and hexadecimal digits", text);
			return 0;
		}
	}

	GameClock start;
	if (wbx_setting_str("clock_start", text, sizeof text) < 0) strcpy(text, "1989-10-25 12:00:00");
	if (!parse_clock(text, &start))
	{
		snprintf(err, (size_t)errsize, "the start date and time is \"%s\"; it is written YYYY-MM-DD HH:MM:SS, from 1980", text);
		return 0;
	}

	if (!check_files(err, errsize)) return 0;
	if (!load_saved_game(err, errsize)) return 0;
	if (g.sound == 'R' && !mt32_open(err, errsize)) return 0;

	g.host = (GameHost) {
		.present = host_present,
		.now = host_now,
		.sleepUntil = host_sleep_until,
		.keyWaiting = host_key_waiting,
		.readKey = host_read_key,
		.start = start,
		.gameDir = ".",
		.noTitle = wbx_setting_bool("skip_title", 0) != 0,
		.ctx = NULL,
		.sound = g.sound ? g.sound : 'N',
		.audio = host_audio,
		.midi = host_midi,
		.joystick = NULL,   /* no joystick, as the setup's answer "N" */
		.seed = seed,       /* every program's random numbers are drawn from it */
		.meleeTickRead = host_melee_tick_read,
	};
	/* the game's whole run on its own stack: the DOS programs keep their state
	 * in their memory image, and the C stack is only the call chain */
	g.co = coro_create(game_body, 8u << 20);
	if (!g.co)
	{
		snprintf(err, (size_t)errsize, "no memory for the game's stack");
		return 0;
	}
	return 1;
}

/* ------------------------------------------------------------------ the step */

void samdrv_frame(int render)
{
	gamestate_to_game();
	g.render = render;
	g.audio_n = 0;
	g.read = 0;
	if (!g.exited)
	{
		/* the first step starts the game, which takes its buttons at its first
		 * look at the host; every later one continues it where the last frame
		 * ended, and it takes them there (settle) */
		if (g.frames == 0 && !g.presented) apply_buttons();
		coro_resume(g.co);
	}
	gamestate_from_game();
}

const uint32_t *samdrv_video(void) { return g.video; }

const int16_t *samdrv_audio(int *samples)
{
	*samples = g.audio_n;
	return g.audio;
}

int samdrv_input_was_read(void) { return 1; }
uint64_t samdrv_frames(void) { return g.frames; }
int samdrv_exited(void) { return g.exited; }
