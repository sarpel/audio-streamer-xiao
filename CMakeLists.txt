cmake_minimum_required(VERSION 3.16.0)

# Ensure ESP-IDF path is available and project.cmake exists to give a clear error instead of a cryptic include failure.
if(NOT DEFINED ENV{IDF_PATH})
	message(FATAL_ERROR "Environment variable IDF_PATH is not set. Source the ESP-IDF export script (export.sh / export.ps1) or set IDF_PATH to your ESP-IDF installation directory.")
endif()

set(IDF_CMAKE_PATH "$ENV{IDF_PATH}/tools/cmake/project.cmake")
if(NOT EXISTS "${IDF_CMAKE_PATH}")
	message(FATAL_ERROR "Could not find ${IDF_CMAKE_PATH}. Verify ESP-IDF is installed and IDF_PATH points to it.")
endif()

# Set the components directory to 'src' instead of 'main'
set(EXTRA_COMPONENT_DIRS src)

include(${IDF_CMAKE_PATH})
project(audio-streamer-xiao)