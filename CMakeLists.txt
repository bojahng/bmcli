cmake_minimum_required(VERSION 3.16)

project(bmcli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(bmcli
  src/main.cpp
)

target_compile_features(bmcli PRIVATE cxx_std_17)

if (MSVC)
  target_compile_options(bmcli PRIVATE /W4)
else()
  target_compile_options(bmcli PRIVATE -Wall -Wextra -Wpedantic)
endif()

