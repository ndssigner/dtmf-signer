add_library(dtmf_core STATIC
  ds_wipe.c
  ds_sha512.c
  ds_sha256.c
  ds_address.c
  ds_ripemd160.c
  ds_bip39.c
  ds_bip32.c
  ds_wallet.c
  ds_tx.c
  ds_psbt.c
  ds_sign.c
  ds_policy.c
  ds_session.c
)
target_include_directories(dtmf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dtmf_core PUBLIC secp256k1 ur_tones PRIVATE ds_warnings)
