cmake_minimum_required(VERSION 3.14)
project(dio_file_manager C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

include(FetchContent)
set(CUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CUI_TEXT_SHAPING ON CACHE BOOL "" FORCE)
FetchContent_Declare(cui GIT_REPOSITORY https://github.com/yappologistic/CUI.git GIT_TAG v1.4.0)
FetchContent_MakeAvailable(cui)

file(GLOB DFM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_executable(dio-file-manager ${DFM_SOURCES})
target_include_directories(dio-file-manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# ui_ctxmenu.c uses CUI's internal popup calls (CUI is pinned to one tag).
set_source_files_properties(src/ui_ctxmenu.c PROPERTIES INCLUDE_DIRECTORIES ${cui_SOURCE_DIR}/src)
target_compile_definitions(dio-file-manager PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(dio-file-manager PRIVATE cui::cui shell32 ole32 oleaut32 uuid shlwapi propsys comctl32)
cui_app_target(dio-file-manager)

# The release exe stays within the size budget of docs/design.md (800 KB).
add_custom_command(TARGET dio-file-manager POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:dio-file-manager> -DCONFIG=$<CONFIG>
          -DMAX_BYTES=819200 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/size_check.cmake
  VERBATIM)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
  enable_testing()
  add_subdirectory(tests)
endif()
