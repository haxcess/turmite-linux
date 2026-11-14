set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../src")
idf_component_register(SRCS "main.c" "board_7b.c"
    "${CORE}/ant.c" "${CORE}/world.c" "${CORE}/rules.c"
    "${CORE}/rng.c" "${CORE}/renderer.c" "${CORE}/scheduler.c"
    INCLUDE_DIRS "." "${CORE}"
    REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio esp_timer esp_psram pthread esp_hw_support)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE _POSIX_C_SOURCE=200809L)
