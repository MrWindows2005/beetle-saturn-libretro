#!/bin/bash
# Links the already-built saturn.a (all .o's from the emscripten platform
# build, archived by hand -- see this repo's top-level Makefile; its own
# final-link rule produces a useless empty-ish default module since nothing
# references retro_init etc as GC roots) together with this directory's
# wrapper.c into the final MODULARIZE'd ES module cores.js expects.
set -e
cd "$(dirname "$0")/.."

EMCC="$(pwd)/../emsdk/upstream/emscripten/emcc.exe"

# "Modo Mega Drive" (see wrapper.c's own comment on it): a completely
# separate, standalone emulated system compiled straight into this same
# module so the page can offer it alongside real Saturn without a second
# .wasm fetch -- see MegaDriveMode/core/core.c's own header comment for
# what it is. Lives outside this submodule at ../../MegaDriveMode.
MD_DIR="$(pwd)/../MegaDriveMode"

"$EMCC" \
  web/wrapper.c saturn.a \
  "$MD_DIR/core/m68k/m68kcpu.c" "$MD_DIR/core/m68k/s68kcpu.c" \
  "$MD_DIR/core/z80/z80.c" \
  "$MD_DIR/core/audio/ym3438.c" "$MD_DIR/core/audio/sn76496.c" \
  "$MD_DIR/core/vdp/vdp.c" \
  "$MD_DIR/core/scd/scd.c" "$MD_DIR/core/scd/cdd.c" "$MD_DIR/core/scd/cdc.c" \
  "$MD_DIR/core/core.c" \
  -I libretro-common/include \
  -I "$MD_DIR/core" -I "$MD_DIR/core/m68k" -I "$MD_DIR/core/z80" -I "$MD_DIR/core/audio" -I "$MD_DIR/core/vdp" -I "$MD_DIR/core/scd" \
  -DHAVE_YM3438_CORE \
  -O2 -pthread \
  -sWASM=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=33554432 \
  -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=2 \
  -sEXPORTED_FUNCTIONS='["_saturn_init","_saturn_load_bios","_saturn_load_cd","_saturn_run_frame","_saturn_framebuffer_ptr","_saturn_framebuffer_width","_saturn_framebuffer_height","_saturn_framebuffer_len","_saturn_audio_ptr","_saturn_audio_samples","_saturn_set_button","_saturn_state_size","_saturn_state_save","_saturn_state_load","_saturn_sram_size","_saturn_sram_get","_saturn_sram_set","_md_web_init","_md_web_load_rom","_md_web_run_frame","_md_web_framebuffer_ptr","_md_web_framebuffer_width","_md_web_framebuffer_height","_md_web_framebuffer_len","_md_web_audio_ptr","_md_web_audio_samples","_md_web_set_button","_malloc","_free"]' \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAP16","HEAPU32","FS"]' \
  -lm \
  -o web/saturn.js

echo "Build OK: web/saturn.js + web/saturn.wasm"
