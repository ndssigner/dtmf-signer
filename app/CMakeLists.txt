add_library(dtmf_app STATIC
  ds_qr.c
  ds_rx.c
  ds_ui.c
  ds_btn.c
  ds_base64.c
  ds_app.c
)
target_include_directories(dtmf_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dtmf_app PUBLIC dtmf_core qrcodegen PRIVATE ds_warnings)
