cmake_minimum_required(VERSION 3.15)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(daydream_steamvr_driver CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch OpenVR Headers
include(FetchContent)
FetchContent_Declare(
    openvr
    GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
    GIT_TAG        v2.0.10
)
FetchContent_MakeAvailable(openvr)

# Driver source files
add_library(driver_daydream SHARED
    src/driver_daydream.cpp
    src/driver_log.cpp
    src/CDaydreamProvider.cpp
    src/CDaydreamController.cpp
    src/DaydreamBLEHandler.cpp
)

# Include directories
target_include_directories(driver_daydream PRIVATE
    ${openvr_SOURCE_DIR}/headers
    src
)

# Libraries needed for C++/WinRT and standard Windows functions (like media keys)
target_link_libraries(driver_daydream PRIVATE
    windowsapp.lib
    user32.lib
)

# Define build platform prefix for SteamVR
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM "win64")
else()
    set(PLATFORM "win32")
endif()

# Set output names and directories as SteamVR expects (e.g., driver_daydream/bin/win64/driver_daydream.dll)
set_target_properties(driver_daydream PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/driver_daydream/bin/${PLATFORM}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/driver_daydream/bin/${PLATFORM}"
)

# Make sure driver resources (like the input profile) are copied next to the bin folder
add_custom_command(TARGET driver_daydream POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "${CMAKE_BINARY_DIR}/driver_daydream/resources"
)
