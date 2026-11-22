# libspeex 1.2.1, narrowband in fixed point (port/config.h), as two targets
# built without the project's warning flags (upstream code, kept quiet):
# speex_nb decodes and encodes (the host build and tools/voice), speex_nb_dec
# only decodes (upstream's DISABLE_ENCODER: the mode tables carry no encoder,
# so none is linked), for the Pico's voice firmware. Its state lives in the caller's memory
# (port/include/spx_arena.h). Added by the root CMakeLists.txt and by the Pico
# port's.
set(SPEEX_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SPEEX_SOURCES
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
foreach(t speex_nb speex_nb_dec)
  add_library(${t} STATIC ${SPEEX_SOURCES})
  target_compile_definitions(${t} PRIVATE HAVE_CONFIG_H)
  target_compile_options(${t} PRIVATE -w)
  target_include_directories(${t}
    PUBLIC ${SPEEX_DIR}/include ${SPEEX_DIR}/port/include
    PRIVATE ${SPEEX_DIR}/port ${SPEEX_DIR}/libspeex ${SPEEX_DIR}/include/speex)
  # vbr.c calls log() and pow(); the C library carries them on macOS and in
  # Emscripten, but glibc and newlib keep them in libm.
  # (by name, not find_library: a cross build must not pick up the host's)
  if(NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(${t} PUBLIC m)
  endif()
endforeach()
target_compile_definitions(speex_nb_dec PRIVATE DISABLE_ENCODER)
