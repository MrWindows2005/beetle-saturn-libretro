// Emscripten-facing wrapper around Beetle Saturn (Mednafen's SS core, this
// fork's libretro.c) -- pure software rendering (no RETRO_ENVIRONMENT_
// SET_HW_RENDER anywhere in this core, unlike Dreamcast/Flycast), so this
// follows the exact same shape as pcsx_rearmed/web/wrapper.c: video_refresh_cb
// converts the core's own pixel format straight into an RGBA8888 buffer,
// exposed through the same <prefix>_framebuffer_ptr/_width/_height/_len
// convention every other core in this project already uses.
//
// Unlike PS1/Dreamcast, Saturn has NO HLE BIOS option at all -- a real
// region-specific BIOS dump is mandatory (see mednafen/ss/ss.c's bios_
// filename selection: "sega_101.bin" for Japan, "mpr-17933.bin" for
// everything else). The actual region is auto-detected from the disc
// itself (beetle_saturn_region core var left unanswered -> setting_region
// stays 0/"auto"), so at BIOS-upload time we don't yet know which of the
// two filenames the loaded disc will resolve to -- write the same
// uploaded bytes to BOTH paths, same "cover every plausible path" pattern
// already used for Sega CD's region-triplicated BIOS (Genesis-Plus-GX/
// web/wrapper_cd.c's gpgx_load_bios()).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <emscripten.h>
#include "libretro.h"

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_sample_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

/* ---- Video: fixed XRGB8888 (see retro_load_game's SET_PIXEL_FORMAT) ---- */
#define FB_MAX_W 704
#define FB_MAX_H 576
static uint32_t out_rgba[FB_MAX_W * FB_MAX_H];
static int out_w = 320, out_h = 240;

static void video_refresh_cb_fn(const void *data, unsigned width, unsigned height, size_t pitch)
{
	int x, y;
	if (!data)
		return; /* duped frame -- nothing to convert */
	if (width > FB_MAX_W) width = FB_MAX_W;
	if (height > FB_MAX_H) height = FB_MAX_H;

	for (y = 0; y < (int)height; y++)
	{
		const uint32_t *src_row = (const uint32_t *)((const uint8_t *)data + y * pitch);
		uint32_t *dst_row = out_rgba + y * width;
		for (x = 0; x < (int)width; x++)
		{
			uint32_t p = src_row[x];
			uint32_t r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
			dst_row[x] = r | (g << 8) | (b << 16) | (0xffu << 24);
		}
	}
	out_w = (int)width;
	out_h = (int)height;
}

/* ---- Audio -------------------------------------------------------------- */
#define AUDIO_MAX_FRAMES 16384
static int16_t audio_buffer[AUDIO_MAX_FRAMES * 2];
static int audio_frames = 0;

static size_t audio_sample_batch_cb_fn(const int16_t *data, size_t frames)
{
	size_t room = AUDIO_MAX_FRAMES - audio_frames;
	size_t n = frames < room ? frames : room;
	memcpy(audio_buffer + audio_frames * 2, data, n * 2 * sizeof(int16_t));
	audio_frames += (int)n;
	return frames;
}

static void audio_sample_cb_fn(int16_t left, int16_t right)
{
	if (audio_frames >= AUDIO_MAX_FRAMES)
		return;
	audio_buffer[audio_frames * 2] = left;
	audio_buffer[audio_frames * 2 + 1] = right;
	audio_frames++;
}

/* ---- Input: standard RETRO_DEVICE_ID_JOYPAD_* order (same as every other
 * wrapper's LIBRETRO_JOYPAD-shaped table -- see input.c's own button
 * remap tables, which all read from these same libretro IDs regardless of
 * which physical Saturn pad button they land on). Only 2 ports driven
 * from JS for now, matching PS1/Dreamcast's own scope cut. */
static int16_t joypad_state[8][16];

static void input_poll_cb_fn(void) {}

static int16_t input_state_cb_fn(unsigned port, unsigned device, unsigned index, unsigned id)
{
	(void)index;
	if (port > 7)
		return 0;
	if (device == RETRO_DEVICE_JOYPAD)
		return id < 16 ? joypad_state[port][id] : 0;
	return 0;
}

/* ---- Logging: this fork's own log_cb already defaults to a real
 * fallback_log (libretro.c line ~77), so -- unlike Flycast -- refusing
 * GET_LOG_INTERFACE here is safe and doesn't crash on the first log call. */

/* libretro-common/time/rtime.c's retro_sleep()/retro_sleep_us() are ONLY
 * implemented for _WIN32 (the whole file is one big #if defined(_WIN32)
 * block with no #else) -- genuinely missing for every non-Windows target
 * in this vendored snapshot, not something specific to us. Called from
 * mednafen/ss/vdp2_render.c's VDP2 render-thread queue backpressure spin
 * (see VDP2REND_EndFrame()/WWQ_Push()). emscripten_sleep() needs Asyncify
 * (not used here) to actually yield without blocking the whole runtime, so
 * this uses pthread's own real usleep() instead, valid since this build
 * targets pthreads (see the Makefile's emscripten platform block). */
#include <unistd.h>
void retro_sleep(unsigned msec) { usleep((useconds_t)msec * 1000); }
void retro_sleep_us(unsigned usec) { usleep((useconds_t)usec); }

static bool environment_cb_fn(unsigned cmd, void *data)
{
	switch (cmd)
	{
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool *)data = true;
		return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = "/system";
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		/* Only XRGB8888 is ever requested (see retro_load_game) -- accept
		 * unconditionally; refusing this would abort loading entirely. */
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE:
	{
		/* Leaving every beetle_saturn_* option unanswered falls through to
		 * this fork's own compiled-in defaults (var.value already starts
		 * NULL'd by libretro.c before each of these calls) -- region stays
		 * "auto" (detected from the disc itself), which is exactly the
		 * real-hardware behavior we want, not something to override. */
		return false;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = false;
		return true;
	case RETRO_ENVIRONMENT_SET_VARIABLES:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	case RETRO_ENVIRONMENT_SET_MESSAGE:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
		return true;
	default:
		return false;
	}
}

EMSCRIPTEN_KEEPALIVE
int saturn_init(void)
{
	memset(joypad_state, 0, sizeof(joypad_state));
	EM_ASM({ FS.mkdirTree('/system'); });

	retro_set_environment(environment_cb_fn);
	retro_init();
	retro_set_video_refresh(video_refresh_cb_fn);
	retro_set_audio_sample(audio_sample_cb_fn);
	retro_set_audio_sample_batch(audio_sample_batch_cb_fn);
	retro_set_input_poll(input_poll_cb_fn);
	retro_set_input_state(input_state_cb_fn);
	return 1;
}

EMSCRIPTEN_KEEPALIVE
void saturn_load_bios(const uint8_t *data, int size)
{
	FILE *f;
	f = fopen("/system/sega_101.bin", "wb");
	if (f) { fwrite(data, 1, size, f); fclose(f); }
	f = fopen("/system/mpr-17933.bin", "wb");
	if (f) { fwrite(data, 1, size, f); fclose(f); }
}

EMSCRIPTEN_KEEPALIVE
int saturn_load_cd(const char *path)
{
	struct retro_game_info info;
	memset(&info, 0, sizeof(info));
	info.path = path;
	info.data = NULL;
	info.size = 0;
	return retro_load_game(&info) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void saturn_run_frame(void)
{
	audio_frames = 0;
	retro_run();
}

EMSCRIPTEN_KEEPALIVE
uint32_t *saturn_framebuffer_ptr(void) { return out_rgba; }

EMSCRIPTEN_KEEPALIVE
int saturn_framebuffer_width(void) { return out_w; }

EMSCRIPTEN_KEEPALIVE
int saturn_framebuffer_height(void) { return out_h; }

EMSCRIPTEN_KEEPALIVE
int saturn_framebuffer_len(void) { return out_w * out_h * 4; }

EMSCRIPTEN_KEEPALIVE
int16_t *saturn_audio_ptr(void) { return audio_buffer; }

EMSCRIPTEN_KEEPALIVE
int saturn_audio_samples(void) { return audio_frames; }

EMSCRIPTEN_KEEPALIVE
void saturn_set_button(int player, int button, int pressed)
{
	if (player < 0 || player >= 8 || button < 0 || button >= 16)
		return;
	joypad_state[player][button] = pressed ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int saturn_state_size(void) { return (int)retro_serialize_size(); }

EMSCRIPTEN_KEEPALIVE
int saturn_state_save(uint8_t *buf, int size) { return retro_serialize(buf, size) ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE
int saturn_state_load(const uint8_t *buf, int size) { return retro_unserialize(buf, size) ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE
int saturn_sram_size(void) { return (int)retro_get_memory_size(RETRO_MEMORY_SAVE_RAM); }

EMSCRIPTEN_KEEPALIVE
int saturn_sram_get(uint8_t *buf, int size)
{
	void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	int real_size = (int)retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	int n;
	if (!data || real_size <= 0)
		return 0;
	n = real_size < size ? real_size : size;
	memcpy(buf, data, n);
	return n;
}

EMSCRIPTEN_KEEPALIVE
int saturn_sram_set(const uint8_t *buf, int size)
{
	void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	int real_size = (int)retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	int n;
	if (!data || real_size <= 0)
		return 0;
	n = real_size < size ? real_size : size;
	memcpy(data, buf, n);
	return 1;
}

/* ---- "Modo Mega Drive" (see cores.js's own comment on it): a completely
 * separate, standalone emulated system -- real Motorola 68000 + real
 * Genesis sound chips + a real, from-scratch VDP (MegaDriveMode's own
 * project, living at ../../MegaDriveMode relative to this file, not
 * inside this submodule -- see that project's own core/core.c header
 * comment). Deliberately bypasses this whole file's own retro_* plumbing
 * above (environment_cb, retro_load_game, retro_run, etc.) instead of
 * going through libretro.c's real "beetle_saturn_system_mode" core-option
 * toggle: MegaDriveMode was never a libretro core in the first place (no
 * retro_load_game/retro_run of its own), and duplicating Mednafen's own
 * real CD-loading machinery just to immediately skip it every single
 * frame would be real, pointless indirection for zero benefit -- calling
 * its own already-clean md_* API directly, exactly like MegaDriveMode's
 * own standalone web/build_web.sh already does, is the real, direct
 * path. Exported here (not a separate WASM module) purely so this one
 * page can offer both "Sega Saturn" and "Modo Mega Drive" without a
 * second network fetch for a second .wasm file. */
extern void md_init(void);
extern void md_load_rom(const uint8_t *data, int size);
extern void md_reset(void);
extern void md_run_frame(void);
extern int md_generate_audio(void);
extern short *md_audio_ptr(void);
extern unsigned int *md_framebuffer_ptr(int *width, int *height);
extern void md_set_scd_mode(int enabled);
extern void md_set_disc_loader(int (*loader)(const char *path));
extern int md_scd_load_disc(const char *path);
extern void md_load_bios(const unsigned char *data, int size);
extern int scd_bridge_load_disc(const char *path);

static uint32_t md_out_rgba[320 * 224];
static int md_out_w = 320, md_out_h = 224;
static int md_out_audio_samples = 0;

EMSCRIPTEN_KEEPALIVE
int md_web_init(void)
{
	md_init();
	return 1;
}

/* Real bug hit and fixed here: this returned void, but cores.js's own
 * generic loadRom() binding does `ok = Module["_${p}_load_rom"](...)`
 * and reports that straight back to main.js as load success/failure --
 * a void return coerces to falsy in JS, so the real ROM load (verified
 * working -- non-silent real audio came out) was being reported as a
 * FAILURE to the rest of the page, which gates its own "loaded OK, show
 * the game" UI on exactly this value. */
EMSCRIPTEN_KEEPALIVE
int md_web_load_rom(const uint8_t *data, int size)
{
	md_load_rom(data, size);
	md_reset();
	return 1;
}

/* Real convention this project's other core wrappers already use (see
 * saturn_run_frame() above and every ${p}_run_frame in cores.js's own
 * bindCore()): one call does BOTH video and audio for the frame, with
 * separate _framebuffer_ptr/_audio_ptr/_audio_samples getters read
 * afterward -- not a separate "generate audio" call the JS side has to
 * remember to make. */
EMSCRIPTEN_KEEPALIVE
void md_web_run_frame(void)
{
	int w, h, x, y;
	unsigned int *src;

	md_run_frame();
	md_out_audio_samples = md_generate_audio();

	src = md_framebuffer_ptr(&w, &h);
	md_out_w = w;
	md_out_h = h;
	/* Real MegaDriveMode framebuffer format is already 0x00RRGGBB per
	 * pixel (see vdp.c's own cram_to_rgb()) -- just needs the same real
	 * RGBA8888 repack video_refresh_cb_fn already does for Saturn's own
	 * frames above, so this page's single <canvas> putImageData path
	 * works unchanged for either system. */
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			uint32_t p = src[y * w + x];
			uint32_t r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
			md_out_rgba[y * w + x] = r | (g << 8) | (b << 16) | (0xffu << 24);
		}
	}
}

EMSCRIPTEN_KEEPALIVE
uint32_t *md_web_framebuffer_ptr(void) { return md_out_rgba; }

EMSCRIPTEN_KEEPALIVE
int md_web_framebuffer_width(void) { return md_out_w; }

EMSCRIPTEN_KEEPALIVE
int md_web_framebuffer_height(void) { return md_out_h; }

EMSCRIPTEN_KEEPALIVE
int md_web_framebuffer_len(void) { return md_out_w * md_out_h * 4; }

EMSCRIPTEN_KEEPALIVE
short *md_web_audio_ptr(void) { return md_audio_ptr(); }

EMSCRIPTEN_KEEPALIVE
int md_web_audio_samples(void) { return md_out_audio_samples; }

/* Real hardware fact: MegaDriveMode has no controller peripheral emulated
 * yet (see core/core.c's own header comment) -- a real no-op, not a
 * missing feature hidden behind a fake success return. cores.js's own
 * bindCore() calls this unconditionally, so it has to exist. */
EMSCRIPTEN_KEEPALIVE
void md_web_set_button(int player, int button, int pressed)
{
	(void)player;
	(void)button;
	(void)pressed;
}

/* Sega CD variant of "Modo Mega Drive" -- separate exported prefix
 * (md_web_cd_*) so cores.js's own generic per-prefix bindCore() can offer
 * it as its own entry in CORES (needsBios/isCd true, unlike the plain
 * cartridge "megadrive" entry above), while still driving the exact same
 * underlying md_* engine and its cartridge-mode-shared md_out_rgba/
 * md_out_w/md_out_h/md_out_audio_samples statics above -- only one "Modo
 * Mega Drive" instance is ever loaded per WASM module (cores.js
 * re-instantiates a fresh Module per core switch, see its own comment on
 * why), so reusing those statics here isn't a real conflict. Real
 * disc-reading hook (scd_bridge_load_disc(), in MegaDriveMode/core/scd/
 * disc_bridge.c) pulls in Saturn's own real mednafen/cdrom chain,
 * already archived into saturn.a for the real Saturn core above -- see
 * this directory's build.sh for the extra disc.c/disc_bridge.c sources
 * and -I path this needs. */
EMSCRIPTEN_KEEPALIVE
int md_web_cd_init(void)
{
	md_set_scd_mode(1);
	md_set_disc_loader(scd_bridge_load_disc);
	md_init();
	return 1;
}

EMSCRIPTEN_KEEPALIVE
int md_web_cd_load_bios(const uint8_t *data, int size)
{
	md_load_bios(data, size);
	return 1;
}

/* Real, established convention this project's other CD cores already use
 * (see saturn_load_cd()/jaguar_load_cd() and cores.js's own loadCd() --
 * `cuePath` is a path already written into this module's Emscripten FS by
 * main.js's loadCdImage(), NOT raw bytes). scd_bridge_load_disc() (via
 * md_scd_load_disc()'s pluggable hook) opens it through Saturn's own real
 * CDIF_Open(), so both a real .cue+.bin set and a real .chd work here
 * exactly like they do for the Saturn/PS1/Dreamcast cores already. */
EMSCRIPTEN_KEEPALIVE
int md_web_cd_load_cd(const char *cue_path)
{
	int ok = md_scd_load_disc(cue_path);
	if (ok) md_reset();
	return ok;
}

EMSCRIPTEN_KEEPALIVE
void md_web_cd_run_frame(void) { md_web_run_frame(); }

EMSCRIPTEN_KEEPALIVE
uint32_t *md_web_cd_framebuffer_ptr(void) { return md_web_framebuffer_ptr(); }

EMSCRIPTEN_KEEPALIVE
int md_web_cd_framebuffer_width(void) { return md_web_framebuffer_width(); }

EMSCRIPTEN_KEEPALIVE
int md_web_cd_framebuffer_height(void) { return md_web_framebuffer_height(); }

EMSCRIPTEN_KEEPALIVE
int md_web_cd_framebuffer_len(void) { return md_web_framebuffer_len(); }

EMSCRIPTEN_KEEPALIVE
short *md_web_cd_audio_ptr(void) { return md_web_audio_ptr(); }

EMSCRIPTEN_KEEPALIVE
int md_web_cd_audio_samples(void) { return md_web_audio_samples(); }

EMSCRIPTEN_KEEPALIVE
void md_web_cd_set_button(int player, int button, int pressed) { md_web_set_button(player, button, pressed); }
