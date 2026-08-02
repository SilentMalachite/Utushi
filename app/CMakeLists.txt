qt_add_executable(utsushi
    WIN32
    MACOSX_BUNDLE
    main.cpp
    main_window.hpp main_window.cpp
    license_dialog.hpp license_dialog.cpp
)
target_link_libraries(utsushi PRIVATE utsushi_core Qt6::Widgets)

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
