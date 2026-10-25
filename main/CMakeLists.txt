file(GLOB_RECURSE srcs "*.cpp" "*.c")

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
)

# board selection (this replaces the platformio.ini build flag)
target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_ESP32S3_N16R8)