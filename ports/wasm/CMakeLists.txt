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
  _dsw_version _dsw_listening _dsw_qr_shown)
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
