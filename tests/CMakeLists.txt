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

utsushi_add_test(tst_converter)

# app/ のソースをテスト実行ファイルへも組み込むバリアント。utsushi_add_test と違い
# app/ の GUI コードを直接コンパイルし Qt6::Widgets をリンクする（Fix wave 2, finding 2:
# app/ には従来ひとつもテストが無く、Converter::resetCancel() 呼び出しの欠落のような
# 「クラッシュもエラーも出ない」不具合を検出できなかった）。
# app/main.cpp（独自の main()）は含めない。QTEST_MAIN は Qt6::Widgets のリンクを検知して
# QApplication を自動生成するため、main.cpp の main() を持ち込むとシンボルが重複する。
function(utsushi_add_app_test name)
    qt_add_executable(${name}
        ${name}.cpp
        ${CMAKE_SOURCE_DIR}/app/main_window.hpp
        ${CMAKE_SOURCE_DIR}/app/main_window.cpp
        ${CMAKE_SOURCE_DIR}/app/license_dialog.hpp
        ${CMAKE_SOURCE_DIR}/app/license_dialog.cpp
    )
    set_target_properties(${name} PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)
    target_link_libraries(${name} PRIVATE utsushi_core Qt6::Widgets Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

utsushi_add_app_test(tst_main_window)
