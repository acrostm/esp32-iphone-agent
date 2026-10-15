# Reuse the platform-independent upstream C implementation without Arduino wrappers.
get_filename_component(SHARED_DIR "${CMAKE_CURRENT_LIST_DIR}/../../hid_bridge/src" ABSOLUTE)
file(GLOB AIRPLAY_SRCS "${SHARED_DIR}/airplay/*.c" "${SHARED_DIR}/airplay/playfair/*.c")
idf_component_register(SRCS "app_main.cpp" "board.cpp" "netcfg.cpp" "ble_link.cpp" "usb_link.cpp" "video_link.cpp"
    "${SHARED_DIR}/bplist/bplist.c" "${SHARED_DIR}/video/gop.c" ${AIRPLAY_SRCS}
    INCLUDE_DIRS "." "../../hid_bridge"
    REQUIRES m5stack__m5unified bt esp_wifi esp_netif esp_event esp_timer esp_psram nvs_flash json
        esp_tinyusb mdns libsodium mbedtls esp_coex)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
# Keep the imported FairPlay algorithm unchanged; its precedence is intentional.
set_source_files_properties("${SHARED_DIR}/airplay/playfair/hand_garble.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-error=parentheses")
