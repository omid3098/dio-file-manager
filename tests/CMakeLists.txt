add_executable(dfm-ops-tests test_ops.c
  ../src/ops.c ../src/clip.c ../src/trash.c
  ../src/search.c ../src/recent.c ../src/shell.c)
target_include_directories(dfm-ops-tests PRIVATE ../src)
target_compile_definitions(dfm-ops-tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(dfm-ops-tests PRIVATE /W4 /utf-8)
target_link_libraries(dfm-ops-tests PRIVATE user32 shell32 ole32 oleaut32 uuid shlwapi propsys)
add_test(NAME dfm-ops COMMAND dfm-ops-tests)
