# Never edit the external submodule. Generate the one-file PAL adaptation
# in the build directory and fail closed if its reviewed input changes.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SUPERPICO_HDMI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/pico_hdmi"
    CACHE PATH "Read-only pico_hdmi checkout (pinned submodule revision)")
get_filename_component(SUPERPICO_HDMI_SOURCE_DIR "${SUPERPICO_HDMI_SOURCE_DIR}" ABSOLUTE)
set(PAL_HDMI_RT "${CMAKE_CURRENT_BINARY_DIR}/pal_hdmi/video_output_rt.c")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_pal_hdmi.py"
        "${SUPERPICO_HDMI_SOURCE_DIR}/src/video_output_rt.c" "${PAL_HDMI_RT}"
    RESULT_VARIABLE PAL_PATCH_RESULT)
if(NOT PAL_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Cannot prepare pinned PAL HDMI backend")
endif()
add_subdirectory("${SUPERPICO_HDMI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/pico_hdmi")
get_target_property(HDMI_SOURCES pico_hdmi SOURCES)
list(REMOVE_ITEM HDMI_SOURCES src/video_output_rt.c)
set_property(TARGET pico_hdmi PROPERTY SOURCES "${HDMI_SOURCES};${PAL_HDMI_RT}")
