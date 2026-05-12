cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/reflect_stub.cmake")

set(LLVM_ROOT "D:/VibeCoding/clang-p2996/build" CACHE PATH "LLVM prefix: bin/clang++, include/c++/v1 if installed")
set(_b "${LLVM_ROOT}/bin")
if(WIN32)
  set(_cx "${_b}/clang++.exe")
  set(_cc "${_b}/clang.exe")
else()
  set(_cx "${_b}/clang++")
  set(_cc "${_b}/clang")
endif()
if(NOT EXISTS "${_cx}")
  message(FATAL_ERROR "clang++ not found: ${_cx}")
endif()
set(CMAKE_CXX_COMPILER "${_cx}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER "${_cc}" CACHE FILEPATH "" FORCE)

project(CppReflection LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
# yaml-cpp 0.8.0 uses a very old inner cmake_minimum_required; CMake 4.0+ aborts unless
# the minimum policy level is raised (see CMake error hint / policy version minimum).
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
endif()
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)
# MinGW-target clang defaults to libstdc++ unless forced; CppReflection uses libc++, so yaml-cpp must match.
if(TARGET yaml-cpp)
  target_compile_options(yaml-cpp PRIVATE -stdlib=libc++)
  target_link_options(yaml-cpp PRIVATE -stdlib=libc++)
endif()

add_executable(
  CppReflection
  main.cpp
  src/hello_reflection.cpp
  src/grammer_and_concepts.cpp
  src/list_of_meta_functions.cpp
  src/implement_enumeration_reflection.cpp
  src/implement_struct_reflection.cpp
)
target_include_directories(CppReflection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(CppReflection PRIVATE -freflection -freflection-latest)
target_link_options(CppReflection PRIVATE -stdlib=libc++)
target_link_libraries(CppReflection PRIVATE yaml-cpp::yaml-cpp)

set(_v1 "${LLVM_ROOT}/include/c++/v1")
set(_src "${LLVM_ROOT}/../libcxx/include")
file(GLOB _trip LIST_DIRECTORIES true "${LLVM_ROOT}/include/*/c++/v1")
if(EXISTS "${_v1}/__config")
  target_include_directories(CppReflection SYSTEM BEFORE PRIVATE "${_v1}")
  if(_trip)
    list(SORT _trip)
    list(GET _trip 0 _tp)
    target_include_directories(CppReflection SYSTEM BEFORE PRIVATE "${_tp}")
  endif()
elseif(EXISTS "${_src}/meta")
  _reflect_write_libcxx_gen("${CMAKE_BINARY_DIR}/libcxx-gen")
  target_include_directories(CppReflection SYSTEM BEFORE PRIVATE "${CMAKE_BINARY_DIR}/libcxx-gen" "${_src}")
  target_compile_options(CppReflection PRIVATE -nostdinc++)
else()
  message(FATAL_ERROR "libc++ not found: ${_v1} ${_src}")
endif()
