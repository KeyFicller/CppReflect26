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

project(CppReflection LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/cppref_github_mirror.cmake")
# yaml-cpp 0.8.0 uses a very old inner cmake_minimum_required; CMake 4.0+ aborts unless
# the minimum policy level is raised (see CMake error hint / policy version minimum).
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
endif()
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
cppref_github_mirror("https://github.com/jbeder/yaml-cpp.git" YAML_CPP_GIT_REPOSITORY)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY "${YAML_CPP_GIT_REPOSITORY}"
  GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)
# MinGW-target clang defaults to libstdc++ unless forced; CppReflection uses libc++, so yaml-cpp must match.
if(TARGET yaml-cpp)
  target_compile_options(yaml-cpp PRIVATE -stdlib=libc++)
  target_link_options(yaml-cpp PRIVATE -stdlib=libc++)
endif()

# Dear ImGui (docking branch): FetchContent (shallow via mirror); sources under build/_deps/imgui-src by default.
cppref_github_mirror("https://github.com/ocornut/imgui.git" IMGUI_GIT_REPOSITORY)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY "${IMGUI_GIT_REPOSITORY}"
  GIT_TAG docking
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
set(CPPREF_IMGUI_ROOT "${imgui_SOURCE_DIR}")
if(NOT EXISTS "${CPPREF_IMGUI_ROOT}/imgui.h")
  message(FATAL_ERROR "FetchContent did not provide Dear ImGui at ${CPPREF_IMGUI_ROOT}")
endif()
file(READ "${CPPREF_IMGUI_ROOT}/imgui.h" CPPREF_IMGUI_H LIMIT 524288)
if(NOT CPPREF_IMGUI_H MATCHES ImGuiConfigFlags_DockingEnable)
  message(FATAL_ERROR "Dear ImGui FetchContent tag must be docking branch (expected ImGuiConfigFlags_DockingEnable in imgui.h)")
endif()
unset(CPPREF_IMGUI_H)

set(IMGUI_CORE_SOURCES
    ${CPPREF_IMGUI_ROOT}/imgui.cpp
    ${CPPREF_IMGUI_ROOT}/imgui_demo.cpp
    ${CPPREF_IMGUI_ROOT}/imgui_draw.cpp
    ${CPPREF_IMGUI_ROOT}/imgui_tables.cpp
    ${CPPREF_IMGUI_ROOT}/imgui_widgets.cpp
    ${CPPREF_IMGUI_ROOT}/misc/cpp/imgui_stdlib.cpp
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
cppref_github_mirror("https://github.com/glfw/glfw.git" GLFW_GIT_REPOSITORY)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY "${GLFW_GIT_REPOSITORY}"
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)
find_package(OpenGL REQUIRED)
set(CPPREF_IMGUI_BACKEND_SOURCES
    ${CPPREF_IMGUI_ROOT}/backends/imgui_impl_glfw.cpp
    ${CPPREF_IMGUI_ROOT}/backends/imgui_impl_opengl3.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_backend_glfw_opengl3.cpp")

if(TARGET glfw)
  target_compile_options(glfw PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
endif()

add_executable(
  CppReflection
  main.cpp
  src/hello_reflection.cpp
  src/grammer_and_concepts.cpp
  src/list_of_meta_functions.cpp
  src/implement_enumeration_reflection.cpp
  src/implement_struct_reflection.cpp
  src/implement_ui_reflection.cpp
)
target_sources(CppReflection PRIVATE ${IMGUI_CORE_SOURCES} ${CPPREF_IMGUI_BACKEND_SOURCES})
target_include_directories(CppReflection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CPPREF_IMGUI_ROOT}"
                                                    "${CPPREF_IMGUI_ROOT}/backends" "${CPPREF_IMGUI_ROOT}/misc/cpp")
target_compile_options(CppReflection PRIVATE -freflection -freflection-latest)
target_link_options(CppReflection PRIVATE -stdlib=libc++)
target_link_libraries(CppReflection PRIVATE yaml-cpp::yaml-cpp glfw OpenGL::GL)

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
