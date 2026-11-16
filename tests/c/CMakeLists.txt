function(ds_add_test name)
  add_executable(${name} ${name}.c)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE dtmf_core ds_warnings)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ds_add_test(test_wipe)
ds_add_test(test_hash)
ds_add_test(test_bip39)
ds_add_test(test_bip32)
ds_add_test(test_wallet)
ds_add_test(test_bitcoin)
ds_add_test(test_tx)

# The PSBT vectors reach the C test as a header generated from the JSON file
# at build time; it lives in the build directory only.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(DS_PSBT_VECTORS_H ${CMAKE_CURRENT_BINARY_DIR}/psbt_vectors.h)
add_custom_command(
  OUTPUT ${DS_PSBT_VECTORS_H}
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/vectors_to_c.py
          ${PROJECT_SOURCE_DIR}/vectors/psbt-v0.json ${DS_PSBT_VECTORS_H}
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/vectors_to_c.py
          ${PROJECT_SOURCE_DIR}/vectors/psbt-v0.json
  COMMENT "Generating psbt_vectors.h"
  VERBATIM)
ds_add_test(test_psbt)
target_sources(test_psbt PRIVATE ${DS_PSBT_VECTORS_H})
target_include_directories(test_psbt PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
ds_add_test(test_sign)
target_sources(test_sign PRIVATE ${DS_PSBT_VECTORS_H})
target_include_directories(test_sign PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
ds_add_test(test_session)
target_sources(test_session PRIVATE ${DS_PSBT_VECTORS_H})
target_include_directories(test_session PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# The tone receiver test needs the app layer and the PSBT vectors.
ds_add_test(test_rx)
target_sources(test_rx PRIVATE ${DS_PSBT_VECTORS_H})
target_include_directories(test_rx PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(test_rx PRIVATE dtmf_app)

# The QR test needs the app layer and the PSBT vectors.
ds_add_test(test_qr)
target_sources(test_qr PRIVATE ${DS_PSBT_VECTORS_H})
target_include_directories(test_qr PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(test_qr PRIVATE dtmf_app)

# The button debouncer and the screen model/drawing need only the app layer
# (the UI test also takes the PSBT summary from the vectors).
ds_add_test(test_btn)
target_link_libraries(test_btn PRIVATE dtmf_app)
ds_add_test(test_ui)
target_sources(test_ui PRIVATE ${DS_PSBT_VECTORS_H})
target_include_directories(test_ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(test_ui PRIVATE dtmf_app)

# The device flow: the app layer, the PSBT vectors and synthesized tones.
ds_add_test(test_app)
target_sources(test_app PRIVATE ${DS_PSBT_VECTORS_H})
target_include_directories(test_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(test_app PRIVATE dtmf_app)

# The Pico port's ADC ring arithmetic is pure C and is tested here.
ds_add_test(test_adc_ring)
target_sources(test_adc_ring PRIVATE ${PROJECT_SOURCE_DIR}/ports/pico/adc_ring.c)
target_include_directories(test_adc_ring PRIVATE ${PROJECT_SOURCE_DIR}/ports/pico)

# Not a test: prints the device's screens for docs/screens.md
# (tools/render_screens.py turns them into PNG files).
add_executable(screens screens.c ${DS_PSBT_VECTORS_H})
target_include_directories(screens PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(screens PRIVATE dtmf_app ds_warnings)

# The review cards: the app layer and the PSBT vectors.
ds_add_test(test_review)
target_sources(test_review PRIVATE ${DS_PSBT_VECTORS_H})
target_include_directories(test_review PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(test_review PRIVATE dtmf_app)

# The voice prompts: the ADPCM decoder (against the shared vector, generated
# into the build directory like the PSBT one) and the pack reader (against the
# built-in English pack, which lives in its own library so the other builds
# skip it, the committed Spanish pack and a data-only Japanese one, both
# turned into a header in the build directory).
set(DS_ADPCM_VECTOR_H ${CMAKE_CURRENT_BINARY_DIR}/adpcm_vector.h)
add_custom_command(
  OUTPUT ${DS_ADPCM_VECTOR_H}
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/adpcm_vector_to_c.py
          ${PROJECT_SOURCE_DIR}/vectors/adpcm-v0.json ${DS_ADPCM_VECTOR_H}
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/adpcm_vector_to_c.py
          ${PROJECT_SOURCE_DIR}/vectors/adpcm-v0.json
  COMMENT "Generating adpcm_vector.h"
  VERBATIM)
set(DS_VOICE_TEST_PACKS_H ${CMAKE_CURRENT_BINARY_DIR}/voice_test_packs.h)
add_custom_command(
  OUTPUT ${DS_VOICE_TEST_PACKS_H}
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/voice_test_packs_to_c.py
          ${PROJECT_SOURCE_DIR}/app/voice/packs/es.bin ${DS_VOICE_TEST_PACKS_H}
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/voice_test_packs_to_c.py
          ${PROJECT_SOURCE_DIR}/tools/voice/make_pack.py
          ${PROJECT_SOURCE_DIR}/tools/voice/adpcm.py
          ${PROJECT_SOURCE_DIR}/tools/voice/phrases.json
          ${PROJECT_SOURCE_DIR}/app/voice/packs/es.bin
  COMMENT "Generating voice_test_packs.h"
  VERBATIM)
ds_add_test(test_voice_pack)
target_sources(test_voice_pack PRIVATE ${DS_ADPCM_VECTOR_H} ${DS_VOICE_TEST_PACKS_H})
target_include_directories(test_voice_pack PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(test_voice_pack PRIVATE dtmf_app ds_voice_packs)
