file(GLOB_RECURSE srcs CONFIGURE_DEPENDS "*.cpp" "*.c")

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
)