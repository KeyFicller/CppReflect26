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

add_executable(
  CppReflection
  main.cpp
  src/hello_reflection.cpp
  src/grammer_and_concepts.cpp
  src/list_of_meta_functions.cpp
  src/implement_enumeration_reflection.cpp
)
target_include_directories(CppReflection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(CppReflection PRIVATE -freflection -freflection-latest)
target_link_options(CppReflection PRIVATE -stdlib=libc++)

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
