set(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/CMake"
  "${CMAKE_MODULE_PATH}"
)

if(APPLE AND EXISTS /usr/local/opt/qt5)
  list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt5")
endif()

cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

project(OSSIA CXX)
find_package(Threads)
find_package(Qt5 5.3 COMPONENTS Core)

# Configuration

# activate select policies
#cmake_policy(VERSION 3.9)
set(OSSIA_3RDPARTY_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")

include(OssiaConfiguration)
cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0056 NEW)
cmake_policy(SET CMP0069 NEW)

# Dependencies
include(CMake/OssiaDeps.cmake)

# Main library
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} /opt/score-sdk/portaudio/lib/cmake/portaudio)
find_package(PortAudio QUIET)
if(NOT PortAudio_FOUND)
  find_package(portaudio QUIET)
endif()
add_definitions(-DFMT_HEADER_ONLY=1)
if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-D_SCL_SECURE_NO_WARNINGS)
endif()
add_subdirectory(OSSIA)

# Examples & tests
if(OSSIA_EXAMPLES)
    add_subdirectory(Documentation/Examples)
endif()

if(OSSIA_TESTING)
  enable_testing()
  add_subdirectory(Tests)
endif()

if(OSSIA_GENVAR)
  add_subdirectory(tools/gen_variant)
endif()
