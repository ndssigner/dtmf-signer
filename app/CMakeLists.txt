add_library(dtmf_app STATIC
  ds_qr.c
  ds_rx.c
  ds_ui.c
  ds_review.c
  ds_about.c
  ds_btn.c
  ds_base64.c
  ds_app.c
)
target_include_directories(dtmf_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dtmf_app PUBLIC dtmf_core qrcodegen PRIVATE ds_warnings)

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
