# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK
include(pico_sdk_import.cmake)

# Pull in FreeRTOS Kernel
include(${CMAKE_CURRENT_LIST_DIR}/FreeRTOS/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

project(mpu6050_freertos C CXX ASM)

# Inicializa o SDK da Pico
pico_sdk_init()


# ============================================================
#  EXECUTÁVEL PRINCIPAL (MPU6050 + SSD1306 + MICROFONE + MQTT)
# ============================================================

add_executable(mpu6050_freertos
        mpu6050_freertos.c
        lib/mpu6050/mpu6050_i2c.c
        lib/ssd1306/ssd1306.c
        local_report.c


        # Arquivos do microfone
        microfone/microphone_dma.c
        microfone/neopixel.c
        microfone/kiss_fft.c
        microfone/kiss_fftr.c

        # MQTT (projeto de sensores)
        mqtt.c
)

pico_set_program_name(mpu6050_freertos "mpu6050_freertos")
pico_set_program_version(mpu6050_freertos "0.1")


# ============================================================
#   GERAR HEADER DO PIO (LEDs - ws2818b.pio)
# ============================================================

pico_generate_pio_header(mpu6050_freertos
        ${CMAKE_CURRENT_LIST_DIR}/microfone/ws2818b.pio
)


# ============================================================
#   STDIO (USB / UART)
# ============================================================

pico_enable_stdio_uart(mpu6050_freertos 0)
pico_enable_stdio_usb(mpu6050_freertos 1)


# ============================================================
#   INCLUDES
# ============================================================

target_include_directories(mpu6050_freertos PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/lib
        ${CMAKE_CURRENT_LIST_DIR}/lib/mpu6050
        ${CMAKE_CURRENT_LIST_DIR}/lib/ssd1306

        # includes do microfone
        ${CMAKE_CURRENT_LIST_DIR}/microfone
        ${CMAKE_CURRENT_LIST_DIR}/microfone/kissfft
)


# ============================================================
#   BIBLIOTECAS NECESSÁRIAS
# ============================================================

target_link_libraries(mpu6050_freertos
        pico_stdlib

        # FreeRTOS
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap4

        # Wi-Fi + MQTT
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_mqtt

        # libs do MPU / display
        hardware_i2c
        hardware_gpio
        hardware_pwm

        # libs usadas pelo microfone
        hardware_dma
        hardware_timer
        hardware_adc
        hardware_pio
        hardware_clocks
)


# ============================================================
#   GERAR ARQUIVOS EXTRAS (UF2 / ELF)
# ============================================================

pico_add_extra_outputs(mpu6050_freertos)
