add_executable(dfm-ops-tests test_ops.c
  ../src/ops.c ../src/clip.c ../src/trash.c
  ../src/search.c ../src/keymatch.c ../src/recent.c ../src/shell.c ../src/dlgtext.c)
target_include_directories(dfm-ops-tests PRIVATE ../src)
target_compile_definitions(dfm-ops-tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(dfm-ops-tests PRIVATE /W4 /utf-8)
target_link_libraries(dfm-ops-tests PRIVATE user32 shell32 ole32 oleaut32 uuid shlwapi propsys advapi32)
add_test(NAME dfm-ops COMMAND dfm-ops-tests)

add_executable(dfm-thumbs-tests test_thumbs.c ../src/thumbs.c)
target_include_directories(dfm-thumbs-tests PRIVATE ../src)
target_compile_definitions(dfm-thumbs-tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(dfm-thumbs-tests PRIVATE /W4 /WX /utf-8)
target_link_libraries(dfm-thumbs-tests PRIVATE shell32 ole32 uuid gdi32 windowscodecs)
add_test(NAME dfm-thumbs COMMAND dfm-thumbs-tests)

add_executable(dfm-foldersize-tests test_foldersize.c ../src/foldersize.c)
target_include_directories(dfm-foldersize-tests PRIVATE ../src)
target_compile_definitions(dfm-foldersize-tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(dfm-foldersize-tests PRIVATE /W4 /WX /utf-8)
add_test(NAME dfm-foldersize COMMAND dfm-foldersize-tests)

add_executable(dfm-treemap-tests test_treemap.c ../src/treemap.c)
target_include_directories(dfm-treemap-tests PRIVATE ../src)
target_compile_options(dfm-treemap-tests PRIVATE /W4 /WX /utf-8)
add_test(NAME dfm-treemap COMMAND dfm-treemap-tests)
add_executable(dfm-agent-tests test_agent.c ../src/agent.c)
target_include_directories(dfm-agent-tests PRIVATE ../src)
target_compile_definitions(dfm-agent-tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(dfm-agent-tests PRIVATE /W3 /utf-8)
add_test(NAME dfm-agent COMMAND dfm-agent-tests)

set(DFM_TEST_WORK "${CMAKE_SOURCE_DIR}/.agent-work/console" CACHE PATH "Console check sandbox parent")
# The checks make their own folders inside it; a fresh clone or a CI runner does not have it yet.
file(MAKE_DIRECTORY "${DFM_TEST_WORK}")
file(TO_NATIVE_PATH "${DFM_TEST_WORK}" DFM_TEST_NATIVE)
string(REPLACE "\\" "\\\\" DFM_TEST_NATIVE "${DFM_TEST_NATIVE}")
foreach(check_target dfm-ops-tests dfm-thumbs-tests dfm-foldersize-tests dfm-agent-tests)
  target_compile_definitions(${check_target} PRIVATE "DFM_TEST_WORK=\"${DFM_TEST_NATIVE}\"")
endforeach()

add_executable(dfm-keymatch-tests test_keymatch.c ../src/keymatch.c)
target_include_directories(dfm-keymatch-tests PRIVATE ../src)
target_compile_definitions(dfm-keymatch-tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(dfm-keymatch-tests PRIVATE /W4 /WX /utf-8)
target_link_libraries(dfm-keymatch-tests PRIVATE user32)
add_test(NAME dfm-keymatch COMMAND dfm-keymatch-tests)

# Console check, no window: the use checks must never move or hold the real pointer (see tests/README.md).
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME dfm-pointer-safe COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_pointer_safe.py)
endif()
