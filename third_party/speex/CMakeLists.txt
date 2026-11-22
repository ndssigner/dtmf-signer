# libspeex 1.2.1, narrowband in fixed point (port/config.h), decoding and
# encoding: its own target, built without the project's warning flags
# (upstream code, kept quiet). Its state lives in the caller's memory
# (port/include/spx_arena.h). Added by the root CMakeLists.txt and by the Pico
# port's.
set(SPEEX_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_library(speex_nb STATIC
  ${SPEEX_DIR}/libspeex/bits.c
  ${SPEEX_DIR}/libspeex/cb_search.c
  ${SPEEX_DIR}/libspeex/exc_10_16_table.c
  ${SPEEX_DIR}/libspeex/exc_10_32_table.c
  ${SPEEX_DIR}/libspeex/exc_20_32_table.c
  ${SPEEX_DIR}/libspeex/exc_5_256_table.c
  ${SPEEX_DIR}/libspeex/exc_5_64_table.c
  ${SPEEX_DIR}/libspeex/exc_8_128_table.c
  ${SPEEX_DIR}/libspeex/filters.c
  ${SPEEX_DIR}/libspeex/gain_table.c
  ${SPEEX_DIR}/libspeex/gain_table_lbr.c
  ${SPEEX_DIR}/libspeex/high_lsp_tables.c
  ${SPEEX_DIR}/libspeex/lpc.c
  ${SPEEX_DIR}/libspeex/lsp.c
  ${SPEEX_DIR}/libspeex/lsp_tables_nb.c
  ${SPEEX_DIR}/libspeex/ltp.c
  ${SPEEX_DIR}/libspeex/modes.c
  ${SPEEX_DIR}/libspeex/nb_celp.c
  ${SPEEX_DIR}/libspeex/quant_lsp.c
  ${SPEEX_DIR}/libspeex/speex.c
  ${SPEEX_DIR}/libspeex/speex_callbacks.c
  ${SPEEX_DIR}/libspeex/vbr.c
  ${SPEEX_DIR}/libspeex/vq.c
  ${SPEEX_DIR}/libspeex/window.c
  ${SPEEX_DIR}/port/spx_arena.c)
target_compile_definitions(speex_nb PRIVATE HAVE_CONFIG_H)
target_compile_options(speex_nb PRIVATE -w)
target_include_directories(speex_nb
  PUBLIC ${SPEEX_DIR}/include ${SPEEX_DIR}/port/include
  PRIVATE ${SPEEX_DIR}/port ${SPEEX_DIR}/libspeex ${SPEEX_DIR}/include/speex)
# vbr.c calls log() and pow(); the C library carries them on macOS and in
# Emscripten, but glibc and newlib keep them in libm.
# (by name, not find_library: a cross build must not pick up the host's)
if(NOT APPLE AND NOT EMSCRIPTEN)
  target_link_libraries(speex_nb PUBLIC m)
endif()
