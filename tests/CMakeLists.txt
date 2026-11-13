add_executable(dfm-ops-tests test_ops.c
  ../src/ops.c ../src/clip.c ../src/trash.c
  ../src/search.c ../src/recent.c ../src/shell.c)
target_include_directories(dfm-ops-tests PRIVATE ../src)
target_compile_definitions(dfm-ops-tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(dfm-ops-tests PRIVATE /W4 /utf-8)
target_link_libraries(dfm-ops-tests PRIVATE user32 shell32 ole32 oleaut32 uuid shlwapi propsys)
add_test(NAME dfm-ops COMMAND dfm-ops-tests)

add_executable(dfm-thumbs-tests test_thumbs.c ../src/thumbs.c)
target_include_directories(dfm-thumbs-tests PRIVATE ../src)
target_compile_definitions(dfm-thumbs-tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(dfm-thumbs-tests PRIVATE /W4 /WX /utf-8)
target_link_libraries(dfm-thumbs-tests PRIVATE shell32 ole32 uuid gdi32 windowscodecs)
add_test(NAME dfm-thumbs COMMAND dfm-thumbs-tests)

set(DFM_TEST_WORK "${CMAKE_SOURCE_DIR}/.agent-work/console" CACHE PATH "Console check sandbox parent")
file(TO_NATIVE_PATH "${DFM_TEST_WORK}" DFM_TEST_NATIVE)
string(REPLACE "\\" "\\\\" DFM_TEST_NATIVE "${DFM_TEST_NATIVE}")
foreach(check_target dfm-ops-tests dfm-thumbs-tests)
  target_compile_definitions(${check_target} PRIVATE "DFM_TEST_WORK=\"${DFM_TEST_NATIVE}\"")
endforeach()
