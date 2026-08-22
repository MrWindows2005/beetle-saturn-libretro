#!/bin/bash
# Links the already-built saturn.a (all .o's from the emscripten platform
# build, archived by hand -- see this repo's top-level Makefile; its own
# final-link rule produces a useless empty-ish default module since nothing
# references retro_init etc as GC roots) together with this directory's
# wrapper.c into the final MODULARIZE'd ES module cores.js expects.
set -e
cd "$(dirname "$0")/.."

EMCC="$(pwd)/../emsdk/upstream/emscripten/emcc.exe"

"$EMCC" \
  web/wrapper.c saturn.a \
  -I libretro-common/include \
  -O2 -pthread \
  -sWASM=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=33554432 \
  -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=2 \
  -sEXPORTED_FUNCTIONS='["_saturn_init","_saturn_load_bios","_saturn_load_cd","_saturn_run_frame","_saturn_framebuffer_ptr","_saturn_framebuffer_width","_saturn_framebuffer_height","_saturn_framebuffer_len","_saturn_audio_ptr","_saturn_audio_samples","_saturn_set_button","_saturn_state_size","_saturn_state_save","_saturn_state_load","_saturn_sram_size","_saturn_sram_get","_saturn_sram_set","_malloc","_free"]' \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAP16","HEAPU32","FS"]' \
  -lm \
  -o web/saturn.js

echo "Build OK: web/saturn.js + web/saturn.wasm"
