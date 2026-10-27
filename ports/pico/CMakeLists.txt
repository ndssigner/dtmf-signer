# DTMF-Signer for the Raspberry Pi Pico (RP2040): a project of its own that
# reuses core/ and app/ unchanged.
#
#   cmake -S ports/pico -B build-pico -DPICO_SDK_PATH=<path to pico-sdk>
#   cmake --build build-pico
#
# gives build-pico/dtmf-signer-pico.uf2. Built and tested with Pico SDK 2.3.1.
# Options:
#   DS_DISPLAY       SH1106_SPI (default, tested on hardware), SSD1306_SPI or
#                    SSD1306_I2C (not tested on hardware)
#   DS_DISPLAY_FLIP  rotate the picture 180 degrees (ON: the breadboard layout
#                    in hardware/breadboard mounts the screen that way round)
#
# The stack of core 0 is 8 KB (see "The stack" below): core 1 must never be
# started.
cmake_minimum_required(VERSION 3.22)

set(PICO_BOARD pico)
set(PICO_PLATFORM rp2040)
include(pico_sdk_import.cmake)

project(dtmf_signer_pico C CXX ASM)

set(DS_DISPLAY "SH1106_SPI" CACHE STRING "Display: SH1106_SPI, SSD1306_SPI or SSD1306_I2C")
set_property(CACHE DS_DISPLAY PROPERTY STRINGS SH1106_SPI SSD1306_SPI SSD1306_I2C)
option(DS_DISPLAY_FLIP "Rotate the display 180 degrees" ON)

if(NOT DS_DISPLAY MATCHES "^(SH1106_SPI|SSD1306_SPI|SSD1306_I2C)$")
  message(FATAL_ERROR "DS_DISPLAY must be SH1106_SPI, SSD1306_SPI or SSD1306_I2C")
endif()

# Read by the SDK's own libraries too, so they are set for the whole project.
add_compile_definitions(
  # 8 KB: see "The stack" below.
  PICO_STACK_SIZE=0x2000
  # The USB echo is output only: nothing is read from USB, and the host
  # cannot reset the board into the bootloader through it.
  PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0
  PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=0
  # A host that stops reading must not stall the audio loop for long.
  PICO_STDIO_USB_STDOUT_TIMEOUT_US=10000)

pico_sdk_init()

get_filename_component(DS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)

# libsecp256k1: what core/ uses (key derivation and ECDSA signing), with
# small tables and no assembly. The optional modules are not used by core/.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SECP256K1_INSTALL OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ECDH OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_EXTRAKEYS OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORRSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_MUSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ELLSWIFT OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SILENTPAYMENTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_ECMULT_WINDOW_SIZE 2 CACHE STRING "" FORCE)
set(SECP256K1_ECMULT_GEN_KB 2 CACHE STRING "" FORCE)
set(SECP256K1_ASM OFF CACHE STRING "" FORCE)

# Our libraries are plain C99 (the same as the host build); the port itself
# uses the SDK's default language level.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_subdirectory(${DS_ROOT}/third_party/secp256k1 secp256k1)

# ur-tones and qrcodegen: their own targets, built without our warning flags.
add_library(ur_tones STATIC ${DS_ROOT}/third_party/ur-tones/c/ur_tones.c)
target_include_directories(ur_tones PUBLIC ${DS_ROOT}/third_party/ur-tones/c)
add_library(qrcodegen STATIC ${DS_ROOT}/third_party/qrcodegen/c/qrcodegen.c)
target_include_directories(qrcodegen PUBLIC ${DS_ROOT}/third_party/qrcodegen/c)

# Warning flags apply to our own targets only. arm-none-eabi-gcc also checks
# that snprintf cannot truncate (-Wformat-truncation) for the screen lines in
# app/, which are fixed-width on purpose: a number too long for its line is
# cut, never overflowed. That check is left off here rather than touching app/.
add_library(ds_warnings INTERFACE)
target_compile_options(ds_warnings INTERFACE -Wall -Wextra -Werror
                                             -Wno-format-truncation)

add_subdirectory(${DS_ROOT}/core core)
add_subdirectory(${DS_ROOT}/app app)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

if(DS_DISPLAY STREQUAL "SH1106_SPI")
  set(DS_DISPLAY_SRC display_sh1106_spi.c)
else()
  set(DS_DISPLAY_SRC display_ssd1306.c)
endif()

add_executable(dtmf-signer-pico main.c adc_ring.c ${DS_DISPLAY_SRC})
target_include_directories(dtmf-signer-pico PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(dtmf-signer-pico PRIVATE DS_DISPLAY_${DS_DISPLAY}=1)
if(DS_DISPLAY_FLIP)
  target_compile_definitions(dtmf-signer-pico PRIVATE DS_DISPLAY_FLIP=1)
endif()
target_link_libraries(dtmf-signer-pico PRIVATE
  dtmf_app dtmf_core
  pico_stdlib hardware_spi hardware_i2c hardware_adc hardware_dma
  hardware_watchdog ds_warnings)

# The stack. Its deepest paths need about 5.5 KB on this build (the review:
# PSBT analysis, BIP32, secp256k1) and 4.7 KB (signing), more than the 4 KB
# of SCRATCH_Y the SDK gives it by default. The SDK's scratch banks X and Y
# are contiguous (0x20040000-0x20042000) and SCRATCH_X is only for core 1's
# stack and code placed with __scratch_x, neither of which this firmware has:
# core 1 is never started, and must not be. So SCRATCH_X is given no length
# and SCRATCH_Y both banks, all of it the core 0 stack (PICO_STACK_SIZE
# 0x2000, __StackBottom..__StackTop = 0x20040000-0x20042000, which main.c
# paints and measures). Anything placed in .scratch_x or .stack1 (core 1's
# default stack: multicore_launch_core1) or in .scratch_y now overflows its
# region and the link fails, instead of sharing memory with the stack.
pico_set_linker_script_var(dtmf-signer-pico SCRATCH_X_LENGTH 0)
pico_set_linker_script_var(dtmf-signer-pico SCRATCH_Y_ORIGIN 0x20040000)
pico_set_linker_script_var(dtmf-signer-pico SCRATCH_Y_LENGTH 0x2000)

pico_enable_stdio_usb(dtmf-signer-pico 1)
pico_enable_stdio_uart(dtmf-signer-pico 0)
pico_add_extra_outputs(dtmf-signer-pico)
