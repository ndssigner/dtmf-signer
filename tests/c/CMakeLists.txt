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
