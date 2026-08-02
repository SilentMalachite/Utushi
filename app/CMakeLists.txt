qt_add_executable(utsushi
    WIN32
    MACOSX_BUNDLE
    main.cpp
    main_window.hpp main_window.cpp
    license_dialog.hpp license_dialog.cpp
)
target_link_libraries(utsushi PRIVATE utsushi_core Qt6::Widgets)

# CI（および将来のパッケージングスクリプト）がビルド成果物の実パスを推測しなくて済むよう、
# CMake が解決した実パスをビルドツリーへ書き出す。qt_standard_project_setup() は Windows で
# は DLL 同居のため CMAKE_RUNTIME_OUTPUT_DIRECTORY を ${CMAKE_BINARY_DIR} 直下に平坦化する
# （全実行ファイルが app/ 等のサブディレクトリに関係なく build/<Config>/ に出る）。macOS では
# バンドルとして別の規約になる。ここではどちらの規約も仮定せず、ジェネレータ式で都度解決する。
if(APPLE)
    set(UTSUSHI_DEPLOY_TARGET_GENEX "$<TARGET_BUNDLE_DIR:utsushi>")
    set(UTSUSHI_ARTIFACT_DIR_GENEX  "$<TARGET_BUNDLE_DIR:utsushi>")
else()
    set(UTSUSHI_DEPLOY_TARGET_GENEX "$<TARGET_FILE:utsushi>")
    set(UTSUSHI_ARTIFACT_DIR_GENEX  "$<TARGET_FILE_DIR:utsushi>")
endif()

file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/utsushi_layout_$<CONFIG>.txt"
    CONTENT
"EXECUTABLE=$<TARGET_FILE:utsushi>
DEPLOY_TARGET=${UTSUSHI_DEPLOY_TARGET_GENEX}
ARTIFACT_DIR=${UTSUSHI_ARTIFACT_DIR_GENEX}
"
)

# ヘルプ > ライセンス ダイアログは「同梱の LICENSE を参照」と案内する。ビルドのたびに
# 実行ファイル/バンドルへ LICENSE を複製し、その案内を実際に成立させる。install() では
# なく POST_BUILD にしているのは、CI がここから直接 macdeployqt / windeployqt を実行し、
# `cmake --install` を経由しないため（ローカルビルドでも追加手順なしで同じ結果になる）。
if(APPLE)
    add_custom_command(TARGET utsushi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "$<TARGET_BUNDLE_CONTENT_DIR:utsushi>/Resources/LICENSE"
        COMMENT "Copying LICENSE into utsushi.app/Contents/Resources"
        VERBATIM
    )
else()
    add_custom_command(TARGET utsushi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "$<TARGET_FILE_DIR:utsushi>/LICENSE"
        COMMENT "Copying LICENSE next to the utsushi executable"
        VERBATIM
    )
endif()
