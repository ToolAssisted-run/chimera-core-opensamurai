/* samurai-driver.h - Sword of the Samurai (OpenSamurai) as a machine that is
 * stepped.
 *
 * The same driver is compiled for the miniBox guest and for the native
 * reference (run-native); wbx-entry.c puts the guest ABI on top of it.
 *
 * Wire format (waterbox.config "input.buttons", same order): the keyboard the
 * game reads, a button for each key. The eight directions are the numeric
 * keypad's (Num Lock off), as the game's keyboard joystick and the published
 * run read them; then the selectors and the scrolls, the battle's orders, the
 * digits, the letters (the samurai's name; R retreats from a battle),
 * Shift, and the game's Alt commands but four: a campaign can be neither
 * saved, restored, abandoned nor quit (Alt+S, Alt+R, Alt+N - which leads back
 * to the career choices and their Restore -, Alt+Q; user-decided,
 * 2026-09-29), and Alt+J is the joystick, which the machine does not have.
 */
#ifndef SAMURAI_DRIVER_H
#define SAMURAI_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum SamButton
{
	/* the directions */
	SAM_BTN_UP,
	SAM_BTN_DOWN,
	SAM_BTN_LEFT,
	SAM_BTN_RIGHT,
	SAM_BTN_UP_LEFT,     /* keypad 7 (Home) */
	SAM_BTN_UP_RIGHT,    /* keypad 9 (PgUp) */
	SAM_BTN_DOWN_LEFT,   /* keypad 1 (End) */
	SAM_BTN_DOWN_RIGHT,  /* keypad 3 (PgDn) */
	/* the selectors */
	SAM_BTN_ENTER,
	SAM_BTN_SPACE,
	SAM_BTN_BACKSPACE,
	SAM_BTN_ESC,
	/* the scrolls */
	SAM_BTN_F1,          /* the Status Scroll */
	SAM_BTN_F2,          /* the Strategic Map */
	SAM_BTN_F3,          /* the Summary Scroll */
	/* the battle's orders */
	SAM_BTN_PLUS,        /* keypad +: turn and march */
	SAM_BTN_MINUS,       /* keypad -: march */
	SAM_BTN_STAR,        /* keypad *: turn */
	SAM_BTN_EQUALS,      /* =: as + */
	/* the digits (the battle's units) */
	SAM_BTN_0,
	SAM_BTN_1, SAM_BTN_2, SAM_BTN_3, SAM_BTN_4, SAM_BTN_5,
	SAM_BTN_6, SAM_BTN_7, SAM_BTN_8, SAM_BTN_9,
	/* the letters */
	SAM_BTN_A,
	SAM_BTN_B, SAM_BTN_C, SAM_BTN_D, SAM_BTN_E, SAM_BTN_F, SAM_BTN_G, SAM_BTN_H, SAM_BTN_I, SAM_BTN_J,
	SAM_BTN_K, SAM_BTN_L, SAM_BTN_M, SAM_BTN_N, SAM_BTN_O, SAM_BTN_P, SAM_BTN_Q, SAM_BTN_R, SAM_BTN_S,
	SAM_BTN_T, SAM_BTN_U, SAM_BTN_V, SAM_BTN_W, SAM_BTN_X, SAM_BTN_Y, SAM_BTN_Z,
	SAM_BTN_SHIFT,
	/* the game's commands */
	SAM_BTN_SOUND,         /* Alt+V: music and effects, effects, silence */
	SAM_BTN_GRAPHICS,      /* Alt+Z: full graphics on or off */
	SAM_BTN_COUNT
};

#define SAM_VIDEO_WIDTH 320
#define SAM_VIDEO_HEIGHT 200

/* The VGA's frame: 3146875 / 44900 = 70.086 Hz, the rate OpenSamurai runs its
 * frames at (game.c: FRAME_CLOCKS of the PIT's 1193182 Hz). */
#define SAM_FRAME_RATE_NUM 3146875
#define SAM_FRAME_RATE_DEN 44900

/* stereo (the MT-32's), 44100 frames a second; a step's is a frame's, about 630 */
#define SAM_AUDIO_RATE 44100
#define SAM_AUDIO_MAX_SAMPLES 4096

/* 0 on failure, with the reason in err */
int samdrv_init(char *err, int errsize);
void samdrv_set_button(int index, int down);
/* runs the program to the end of its next video frame */
void samdrv_frame(int render);
const uint32_t *samdrv_video(void);
const int16_t *samdrv_audio(int *samples);   /* stereo frames, left then right */
int samdrv_input_was_read(void);
uint64_t samdrv_frames(void);
/* the program ended (the player quit): every step after is a frame of nothing */
int samdrv_exited(void);

/* files.c: the game's files. The project's are mounted under their names and
 * read as they are; what the game writes (its saved game) lives in guest
 * memory. The names the game may look up in its directory are these. */
void files_set_known(const char *const *names, int count);
int files_saved(const char *name, const uint8_t **data, long *len);

/* game-state.c: the domains and the property table */
int samdrv_domain_count(void);
const char *samdrv_domain_name(int i);
uint8_t *samdrv_domain_ptr(int i);
int64_t samdrv_domain_size(int i);
int samdrv_domain_writable(int i);
const char *samdrv_game_properties(void);
void gamestate_from_game(void);
void gamestate_to_game(void);

#ifdef __cplusplus
}
#endif

#endif
