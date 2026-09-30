cmake_minimum_required(VERSION 3.20)
project(OctoFog VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "OctoFog currently builds on Windows only.")
endif()

add_library(OctoFog SHARED
  src/proxy.cpp
  src/config.cpp
)

target_compile_features(OctoFog PRIVATE cxx_std_20)
target_compile_definitions(OctoFog PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(OctoFog PRIVATE src)

set_target_properties(OctoFog PROPERTIES
  OUTPUT_NAME "d3d9"
  PREFIX ""
)

if(MSVC)
  target_compile_options(OctoFog PRIVATE /W4 /permissive- /EHsc)
endif()

install(TARGETS OctoFog RUNTIME DESTINATION . LIBRARY DESTINATION .)
install(FILES OctoFog.ini DESTINATION .)
