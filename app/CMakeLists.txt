add_library(dtmf_app STATIC
  ds_qr.c
  ds_rx.c
  ds_ui.c
  ds_review.c
  ds_about.c
  ds_btn.c
  ds_base64.c
  ds_speex.c
  ds_crc32.c
  ds_pack.c
  ds_app.c
  ds_say.c
  ds_multitap.c
  ds_voice.c
  voice/ds_prompt_names.c
)
target_include_directories(dtmf_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/voice)
target_link_libraries(dtmf_app PUBLIC dtmf_core qrcodegen PRIVATE speex_nb ds_warnings)

# The built-in English prompts are data (hundreds of kilobytes): a library of
# their own, linked only by the builds that speak, so the Cable and QR
# firmware does not grow. Other languages are separate packs
# (voice/packs/*.bin) written to their own flash region.
add_library(ds_voice_packs STATIC voice/pack_en.c)
target_include_directories(ds_voice_packs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/voice)
target_link_libraries(ds_voice_packs PRIVATE ds_warnings)

# The version the About page shows: git describe when the build is
# configured (re-run cmake after a commit to update it).
execute_process(
  COMMAND git describe --tags --always --dirty
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE DS_VERSION_GIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT DS_VERSION_GIT)
  set(DS_VERSION_GIT "unknown")
endif()
target_compile_definitions(dtmf_app PUBLIC "DS_VERSION=\"${DS_VERSION_GIT}\"")
