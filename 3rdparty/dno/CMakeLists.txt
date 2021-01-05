cmake_minimum_required(VERSION 3.9)
project(dno LANGUAGES CXX)
add_library(dno 
  src/denoise.cpp
  src/denoise.hpp
  include/1EuroFilter.hpp
)
target_include_directories(rnd PUBLIC 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(rnd PUBLIC cxx_std_17)

include(GenerateExportHeader)
generate_export_header(dno)
