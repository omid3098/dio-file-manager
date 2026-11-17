cmake_minimum_required(VERSION 3.14)
project(dio_file_manager C RC)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

include(FetchContent)
set(CUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CUI_TEXT_SHAPING ON CACHE BOOL "" FORCE)
# CUI comes from omid3098's fork of yappologistic/CUI, branch dfm-rtl-input: upstream plus pull request #12, which makes
# text boxes join and edit Persian right to left. While #12 is open, moving CUI to upstream's newest commit means
# rebasing dfm-rtl-input onto it, pushing it to the fork, and pinning the new commit here. See docs/architecture.md.
FetchContent_Declare(cui GIT_REPOSITORY https://github.com/omid3098/CUI-yappologistic.git GIT_TAG 57ccb47d4373e4c36788162b0b43480b88296c55)
FetchContent_MakeAvailable(cui)
# Stops a CUI without the right-to-left text field editing (a plain upstream pin) before it builds an app that types Persian wrong.
file(STRINGS ${cui_SOURCE_DIR}/src/cui_textedit.c DFM_CUI_RTL LIMIT_COUNT 1 REGEX "cui__paint_edit_shaped")
if(NOT DFM_CUI_RTL)
  message(FATAL_ERROR "The CUI at ${cui_SOURCE_DIR} lacks right-to-left text field editing (function cui__paint_edit_shaped in "
    "src/cui_textedit.c), so Persian typed into the text boxes would draw unjoined and left to right. Pin the CUI fork "
    "https://github.com/omid3098/CUI-yappologistic.git at a commit of branch dfm-rtl-input (upstream plus pull request #12): "
    "rebase that branch onto upstream's newest commit, push it, and put the new commit in FetchContent_Declare. See docs/architecture.md.")
endif()

file(GLOB DFM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
# The .rc embeds the icon (resource id 1: CUI loads it as the window class icon).
add_executable(dio-file-manager ${DFM_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/src/dio-file-manager.rc)
target_include_directories(dio-file-manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# ui_ctxmenu.c uses CUI's internal popup calls, and ui_chrome.c its internal hit strip for the sidebar edge (CUI is pinned to one tag).
# The internal context layout must match the text-shaping build of CUI.
set_source_files_properties(src/ui_ctxmenu.c src/ui_chrome.c PROPERTIES INCLUDE_DIRECTORIES ${cui_SOURCE_DIR}/src
  COMPILE_DEFINITIONS CUI_TEXT_SHAPING)
target_compile_definitions(dio-file-manager PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(dio-file-manager PRIVATE cui::cui shell32 ole32 oleaut32 uuid shlwapi propsys advapi32 comctl32 dwmapi setupapi cfgmgr32)
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
