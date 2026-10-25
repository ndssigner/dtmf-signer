add_library(dtmf_core STATIC
  ds_wipe.c
  ds_sha512.c
  ds_ripemd160.c
  ds_bip39.c
  ds_bip32.c
)
target_include_directories(dtmf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dtmf_core PUBLIC secp256k1 ur_tones PRIVATE ds_warnings)
