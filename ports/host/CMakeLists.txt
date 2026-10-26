add_executable(dtmf-signer-host main.c wav.c base64.c)
target_include_directories(dtmf-signer-host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dtmf-signer-host PRIVATE dtmf_core ds_warnings)
