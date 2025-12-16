# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Must set these FIRST
set(PICO_BOARD pico_w)
set(PICO_CYW43_ARCH "pico_cyw43_arch_lwip_threadsafe_background")

# Global settings (before project/sdk init)
set(__USE_PICO_STATIC_ASSERT 1)
set(PICO_CYW43_ARCH_LWIP 1)
set(PICO_CYW43_ARCH_ENABLE_BT 1)  # Only if you need Bluetooth

include(pico_sdk_import.cmake)
project(wifi C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICOTOOL_FORCE_FETCH_FROM_GIT OFF)
set(PICO_TOOLCHAIN_PROGRAMMING_VERSION 2.1.1)

# Copy firmware files to build directory
file(COPY ${PICO_SDK_PATH}/lib/cyw43-driver/firmware/
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/firmware)

pico_sdk_init()

add_definitions(-DMBEDTLS_NO_PLATFORM_ENTROPY)
add_definitions(-DMBEDTLS_ENTROPY_HARDWARE_ALT)

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_AS_SUBPROJECT ON)

add_subdirectory(${PICO_SDK_PATH}/lib/mbedtls ${CMAKE_CURRENT_BINARY_DIR}/mbedtls)

add_executable(wifi 
	src/main.c
	src/wifi.c
	src/bme680.c
	src/bme68x.c
	src/mqtt_client.c
	src/pico_transport.c
	src/datetime.c
	#src/mqtt_lib/MQTTPacket/src/MQTTPacket.c
	#src/mqtt_lib/MQTTPacket/src/MQTTSerializePublish.c
	#src/mqtt_lib/MQTTPacket/src/MQTTConnectClient.c
	#src/mqtt_lib/MQTTPacket/src/MQTTConnectServer.c
	#src/mqtt_lib/MQTTPacket/src/MQTTDeserializePublish.c
	#src/mqtt_lib/MQTTPacket/src/MQTTUnsubscribeClient.c
	#src/mqtt_lib/MQTTPacket/src/MQTTUnsubscribeServer.c
	src/mqtt_lib/PahoEmbeddedC/MQTTClient-C/src/MQTTClient.c
	src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src/MQTTPacket.c
	src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src/MQTTConnectClient.c
	src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src/MQTTConnectServer.c
    	src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src/MQTTDeserializePublish.c
    	src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src/MQTTSerializePublish.c
    	src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src/MQTTSubscribeClient.c
   	src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src/MQTTSubscribeServer.c
    	src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src/MQTTUnsubscribeClient.c
    	src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src/MQTTUnsubscribeServer.c
)

pico_set_program_name(wifi "wifi")

# Now that 'main' target exists, we can configure it
target_compile_definitions(wifi PRIVATE
    __USE_PICO_STATIC_ASSERT
    CYW43_LWIP=1
    CYW43_WIFI_NVRAM_INCLUDE_FILE="wifi_nvram_43439.h"
)

target_compile_definitions(wifi PRIVATE
    CYW43_HAL_GET_MAC_DEFINED=1  # Prevent redefinition
    MBEDTLS_ALLOW_PRIVATE_ACCESS
)

target_include_directories(wifi PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PICO_SDK_PATH}/lib/cyw43-driver/firmware
    ${PICO_SDK_PATH}/lib/cyw43-driver/src
    ${PICO_SDK_PATH}/lib/cyw43-driver/include
    ${PICO_SDK_PATH}/lib/lwip/src/include
    ${PICO_SDK_PATH}/lib/lwip/contrib/ports/rp2040/include
    ${PICO_SDK_PATH}/src/rp2_common/pico_cyw43_driver
    ${PICO_SDK_PATH}/src/common/pico_stdlib/include
    #${PICO_SDK_PATH}/src/common/pico_base/include
    #${PICO_SDK_PATH}/src/common/pico_time/include
    #${PICO_SDK_PATH}/src/common/pico_types/include
    #${PICO_SDK_PATH}/src/common/pico_sync/include
    #${PICO_SDK_PATH}/src/common/pico_util/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_gpio/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_uart/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_spi/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_i2c/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_pwm/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_adc/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_rtc/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_flash/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_pll/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_vreg/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_clocks/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_watchdog/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_xosc/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_irq/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_pio/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_dma/include
    #${PICO_SDK_PATH}/src/rp2_common/hardware_claim/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_platform/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_printf/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_bit_ops/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_bootrom/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_divider/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_double/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_int64_ops/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_float/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_malloc/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_mem_ops/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_stdio/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_stdio_usb/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_unique_id/include
    #${PICO_SDK_PATH}/src/rp2_common/pico_runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # ${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt_lib/MQTTPacket/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt_lib/PahoEmbeddedC/MQTTClient-C/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt_lib/PahoEmbeddedC/MQTTPacket/src
    ${PICO_SDK_PATH}/lib/mbedtls/include
)

target_link_libraries(wifi
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_lwip
    pico_lwip_mbedtls
    pico_lwip_sntp
    mbedtls
    mbedx509
    mbedcrypto
    hardware_pio
    hardware_dma
    hardware_irq
    hardware_i2c
)

pico_enable_stdio_usb(wifi 1)
pico_enable_stdio_uart(wifi 0)
pico_add_extra_outputs(wifi)
