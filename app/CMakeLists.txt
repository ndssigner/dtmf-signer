add_library(dtmf_app STATIC
  ds_qr.c
  ds_rx.c
  ds_ui.c
  ds_btn.c
)
target_include_directories(dtmf_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dtmf_app PUBLIC dtmf_core qrcodegen PRIVATE ds_warnings)
