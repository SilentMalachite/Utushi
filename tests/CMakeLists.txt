# テスト追加用ヘルパ。utsushi_core ができる Task 3 以降は
# target_link_libraries に utsushi_core を足した版に更新する。
function(utsushi_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    set_target_properties(${name} PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)
    target_link_libraries(${name} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

utsushi_add_test(tst_no_hardcode)
target_compile_definitions(tst_no_hardcode PRIVATE UTSUSHI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
