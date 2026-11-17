# DTMF-Signer as WebAssembly, for the website's simulator: built by
# tools/build-wasm.sh with Emscripten (the root CMakeLists.txt adds only
# core, app and this port when EMSCRIPTEN is set). Gives dtmf-signer.mjs,
# an ES module with the wasm inside (no separate download).
add_executable(dtmf-signer-wasm main.c)
target_link_libraries(dtmf-signer-wasm PRIVATE dtmf_app dtmf_core ds_warnings)
set_target_properties(dtmf-signer-wasm PROPERTIES OUTPUT_NAME dtmf-signer SUFFIX .mjs)
set(DSW_EXPORTS
  _dsw_init _dsw_audio_buf _dsw_audio _dsw_advance _dsw_take_dirty _dsw_draw _dsw_contrast
  _dsw_rows _dsw_line _dsw_picker_cell _dsw_take_echo _dsw_echo _dsw_keys _dsw_wants_reboot
  _dsw_version _dsw_listening _dsw_qr_shown _dsw_qr_kind)
list(JOIN DSW_EXPORTS "," DSW_EXPORTS_CSV)
target_link_options(dtmf-signer-wasm PRIVATE
  -O2
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sSINGLE_FILE=1
  -sENVIRONMENT=web,node
  -sALLOW_MEMORY_GROWTH=0
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP16,UTF8ToString
  -sEXPORTED_FUNCTIONS=${DSW_EXPORTS_CSV})

# The voice firmware (the Centralita telephone exchange) as a second module,
# dtmf-signer-voice.mjs: JavaScript only moves the line samples, the hook and
# the pack region. Memory cannot grow, so it is sized here: the English pack
# (~700 KB of data), the 1 MB pack region, ds_voice (~80 KB), the buffers and
# the stack, with room to spare.
add_executable(dtmf-signer-voice-wasm voice.c)
target_link_libraries(dtmf-signer-voice-wasm PRIVATE dtmf_app dtmf_core ds_voice_packs ds_warnings)
set_target_properties(dtmf-signer-voice-wasm PROPERTIES OUTPUT_NAME dtmf-signer-voice SUFFIX .mjs)
set(DVW_EXPORTS
  _dvw_region_buf _dvw_region_len _dvw_init _dvw_hook _dvw_advance _dvw_in_buf _dvw_in
  _dvw_out_buf _dvw_out _dvw_take_echo _dvw_echo _dvw_keys _dvw_version)
list(JOIN DVW_EXPORTS "," DVW_EXPORTS_CSV)
target_link_options(dtmf-signer-voice-wasm PRIVATE
  -O2
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sSINGLE_FILE=1
  -sENVIRONMENT=web,node
  -sALLOW_MEMORY_GROWTH=0
  -sINITIAL_MEMORY=8388608
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP16,UTF8ToString
  -sEXPORTED_FUNCTIONS=${DVW_EXPORTS_CSV})

# What the About pages say about this build (its capabilities are the
# Cable and QR profile's, the default in app/ds_about.h).
target_compile_definitions(dtmf_app PUBLIC "DS_HARDWARE=\"Web simulator (wasm)\"")
