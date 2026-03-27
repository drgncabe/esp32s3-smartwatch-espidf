# ESP-IDF Component Registration for src directory

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "../include"
    REQUIRES 
        esp_wifi 
        esp_netif 
        esp_event
        nvs_flash
        driver
        spi_flash
        bt
        esp_http_server
        esp_http_client
        lwip
        esp_eth
        esp_timer
        freertos
        esp_system
        esp_rom
)
