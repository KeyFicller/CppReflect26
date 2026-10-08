cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# clang-p2996 (P2996 static reflection), installed locally:
#   <LLVM_ROOT>/bin/clang++          compiler; the driver finds the libc++ below
#   <LLVM_ROOT>/include/c++/v1/meta  reflective libc++ headers
#   <LLVM_ROOT>/lib/libc++.dylib     matching runtime
# ---------------------------------------------------------------------------
set(LLVM_ROOT "/Users/keyficller/src/llvm-p2996" CACHE PATH "clang-p2996 install prefix")

set(_cx "${LLVM_ROOT}/bin/clang++")
set(_cc "${LLVM_ROOT}/bin/clang")
if(NOT EXISTS "${_cx}")
  message(FATAL_ERROR "clang++ not found: ${_cx} (set LLVM_ROOT)")
endif()
set(CMAKE_CXX_COMPILER "${_cx}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER "${_cc}" CACHE FILEPATH "" FORCE)

project(CppReflection LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Single-config generator (Ninja) would otherwise build with no flags at all,
# i.e. no -g and therefore no debug info for breakpoints.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

if(NOT EXISTS "${LLVM_ROOT}/include/c++/v1/meta")
  message(FATAL_ERROR "reflective libc++ not found: ${LLVM_ROOT}/include/c++/v1/meta")
endif()

# macOS SDK headers: a source-built clang has no default sysroot.
execute_process(COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE _sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _sdk)
  message(FATAL_ERROR "xcrun --show-sdk-path failed")
endif()

add_compile_options(-isysroot "${_sdk}" "$<$<COMPILE_LANGUAGE:CXX>:-freflection-latest>")
# Link the reflective runtime from the prefix instead of /usr/lib.
add_link_options(-isysroot "${_sdk}" -L "${LLVM_ROOT}/lib" "-Wl,-rpath,${LLVM_ROOT}/lib")

# ---------------------------------------------------------------------------
# Third-party dependencies.
# ---------------------------------------------------------------------------
include(FetchContent)

# github.com clones are unreliable on this network, so fetch from the gitee
# mirrors by default (same tags/commits). Configure with -DCPPREF_DEPS_FROM_GITHUB=ON
# to use github.com directly.
option(CPPREF_DEPS_FROM_GITHUB "Fetch dependencies from github.com instead of the gitee mirrors" OFF)
if(CPPREF_DEPS_FROM_GITHUB)
  set(CPPREF_YAML_CPP_URL "https://github.com/jbeder/yaml-cpp.git" CACHE STRING "")
  set(CPPREF_GLFW_URL "https://github.com/glfw/glfw.git" CACHE STRING "")
  set(CPPREF_IMGUI_URL "https://github.com/ocornut/imgui.git" CACHE STRING "")
else()
  set(CPPREF_YAML_CPP_URL "https://gitee.com/mirrors/yaml-cpp.git" CACHE STRING "")
  set(CPPREF_GLFW_URL "https://gitee.com/mirrors/glfw.git" CACHE STRING "")
  set(CPPREF_IMGUI_URL "https://gitee.com/mirrors/imgui.git" CACHE STRING "")
endif()

# yaml-cpp 0.8.0 declares a pre-3.5 cmake_minimum_required, which CMake 4 aborts on.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY "${CPPREF_YAML_CPP_URL}"
  GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

# Dear ImGui (docking branch): sources only, the repo has no CMakeLists.
FetchContent_Declare(
  imgui
  GIT_REPOSITORY "${CPPREF_IMGUI_URL}"
  GIT_TAG docking
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
set(CPPREF_IMGUI_ROOT "${imgui_SOURCE_DIR}")
if(NOT EXISTS "${CPPREF_IMGUI_ROOT}/imgui.h")
  message(FATAL_ERROR "Dear ImGui sources not found at ${CPPREF_IMGUI_ROOT}")
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY "${CPPREF_GLFW_URL}"
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# CppReflection executable.
# ---------------------------------------------------------------------------
set(IMGUI_SOURCES
    ${CPPREF_IMGUI_ROOT}/imgui.cpp
    ${CPPREF_IMGUI_ROOT}/imgui_demo.cpp
    ${CPPREF_IMGUI_ROOT}/imgui_draw.cpp
    ${CPPREF_IMGUI_ROOT}/imgui_tables.cpp
    ${CPPREF_IMGUI_ROOT}/imgui_widgets.cpp
    ${CPPREF_IMGUI_ROOT}/misc/cpp/imgui_stdlib.cpp
    ${CPPREF_IMGUI_ROOT}/backends/imgui_impl_glfw.cpp
    ${CPPREF_IMGUI_ROOT}/backends/imgui_impl_opengl3.cpp)

add_executable(
  CppReflection
  main.cpp
  src/hello_reflection.cpp
  src/grammer_and_concepts.cpp
  src/list_of_meta_functions.cpp
  src/implement_enumeration_reflection.cpp
  src/implement_struct_reflection.cpp
  src/implement_ui_reflection.cpp
  src/implement_undo_redo_reflection.cpp
  src/implement_ffn_reflection.cpp
  src/imgui_backend_glfw_opengl3.cpp)
target_sources(CppReflection PRIVATE ${IMGUI_SOURCES})
target_include_directories(CppReflection PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CPPREF_IMGUI_ROOT}"
    "${CPPREF_IMGUI_ROOT}/backends"
    "${CPPREF_IMGUI_ROOT}/misc/cpp")
target_link_libraries(CppReflection PRIVATE yaml-cpp::yaml-cpp glfw OpenGL::GL)

# ld64 leaves DWARF in the object files; lldb needs a .dSYM beside the binary
# to bind breakpoints. (macOS)
if(APPLE)
  find_program(CPPREF_DSYMUTIL dsymutil)
  if(CPPREF_DSYMUTIL)
    add_custom_command(TARGET CppReflection POST_BUILD
      COMMAND "${CPPREF_DSYMUTIL}" "$<TARGET_FILE:CppReflection>"
      COMMENT "Generating dSYM for CppReflection"
      VERBATIM)
  endif()
endif()
