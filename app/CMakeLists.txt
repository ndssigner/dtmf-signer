add_library(dtmf_app STATIC
  ds_rx.c
)
target_include_directories(dtmf_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dtmf_app PUBLIC dtmf_core PRIVATE ds_warnings)
