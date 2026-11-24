cmake_minimum_required(VERSION 3.24)

project(SPSArousalMorphs VERSION 1.3.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMMONLIB_SSE_SOURCE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../codex-references/native/CommonLibSSE-NG"
  CACHE PATH "Path to the CommonLibSSE-NG source tree")

if(DEFINED ENV{COMMONLIB_SSE_FOLDER})
  set(COMMONLIB_SSE_SOURCE_DIR "$ENV{COMMONLIB_SSE_FOLDER}")
endif()

if(NOT EXISTS "${COMMONLIB_SSE_SOURCE_DIR}/CMakeLists.txt")
  set(_legacy_commonlib "${CMAKE_CURRENT_SOURCE_DIR}/../.sps-deps/CommonLibSSE-NG")
  if(EXISTS "${_legacy_commonlib}/CMakeLists.txt")
    message(STATUS "CommonLibSSE-NG was not found at ${COMMONLIB_SSE_SOURCE_DIR}; "
      "falling back to the legacy copy at ${_legacy_commonlib}")
    set(COMMONLIB_SSE_SOURCE_DIR "${_legacy_commonlib}")
  else()
    message(FATAL_ERROR
      "CommonLibSSE-NG was not found at ${COMMONLIB_SSE_SOURCE_DIR}. "
      "Set COMMONLIB_SSE_SOURCE_DIR (or COMMONLIB_SSE_FOLDER) to its source tree.")
  endif()
endif()

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
# Declared without FORCE so -DENABLE_SKYRIM_VR=ON still works: this must run
# before add_subdirectory, because CommonLibSSE-NG's own option() defaults VR on
# and would otherwise define the cache entry first.
option(ENABLE_SKYRIM_VR "Add Skyrim VR 1.4.15 support (needs the openvr submodule)" OFF)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${COMMONLIB_SSE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/commonlib")
list(APPEND CMAKE_MODULE_PATH "${COMMONLIB_SSE_SOURCE_DIR}/cmake")
include(CommonLibSSE)
find_package(SimpleIni CONFIG QUIET)

set(SKSE_MENU_FRAMEWORK_INCLUDE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../codex-references/ui"
  CACHE PATH "Directory containing SKSE Menu Framework 3's SKSEMenuFramework.h")
if(NOT EXISTS "${SKSE_MENU_FRAMEWORK_INCLUDE_DIR}/SKSEMenuFramework.h")
  set(_legacy_menu_framework
    "${CMAKE_CURRENT_SOURCE_DIR}/../.sps-deps/SKSE-Menu-Framework-3-Example/include")
  if(EXISTS "${_legacy_menu_framework}/SKSEMenuFramework.h")
    message(STATUS "SKSEMenuFramework.h was not found in ${SKSE_MENU_FRAMEWORK_INCLUDE_DIR}; "
      "falling back to the legacy copy at ${_legacy_menu_framework}")
    set(SKSE_MENU_FRAMEWORK_INCLUDE_DIR "${_legacy_menu_framework}")
  else()
    message(FATAL_ERROR
      "SKSEMenuFramework.h was not found in ${SKSE_MENU_FRAMEWORK_INCLUDE_DIR}. "
      "Set SKSE_MENU_FRAMEWORK_INCLUDE_DIR to the directory that contains it.")
  endif()
endif()
message(STATUS "SKSE Menu Framework header: ${SKSE_MENU_FRAMEWORK_INCLUDE_DIR}/SKSEMenuFramework.h")

add_commonlibsse_plugin(${PROJECT_NAME}
  AUTHOR "snowman12356"
  SOURCES
    src/DebugMenu.cpp
    src/DebugMenu.h
    src/LengthController.cpp
    src/LengthController.h
    src/MorphController.cpp
    src/MorphController.h
    src/NpcController.cpp
    src/NpcController.h
    src/PCH.h
    src/SKEEAPI.h
    src/SPSAPI.h
    src/SPSBridge.cpp
    src/SPSBridge.h
    src/Settings.cpp
    src/Settings.h
    src/plugin.cpp)

target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
  "${SKSE_MENU_FRAMEWORK_INCLUDE_DIR}")
if(TARGET SimpleIni::SimpleIni)
  target_link_libraries(${PROJECT_NAME} PRIVATE SimpleIni::SimpleIni)
else()
  target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
endif()
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
endif()

install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION "SKSE/Plugins"
  COMPONENT SPSArousalMorphs)
install(FILES config/SPSArousalMorphs.ini
  DESTINATION "SKSE/Plugins"
  COMPONENT SPSArousalMorphs)
install(FILES README.md
  DESTINATION "."
  COMPONENT SPSArousalMorphs)
