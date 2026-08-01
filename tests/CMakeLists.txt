# テスト追加用ヘルパ。utsushi_core をリンクする（Task 3 以降）。
function(utsushi_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    set_target_properties(${name} PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)
    target_link_libraries(${name} PRIVATE utsushi_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

utsushi_add_test(tst_no_hardcode)
target_compile_definitions(tst_no_hardcode PRIVATE UTSUSHI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

utsushi_add_test(tst_page_range)

utsushi_add_test(tst_render_size)

utsushi_add_test(tst_output_path)
