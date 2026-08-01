# Utsushi 進捗ログ

## 2026-08-02
- リポジトリ初期化。CMake スケルトンと空ウィンドウの app を作成。
- 決定: qt-conventions.md を docs/ へ移動（CLAUDE.md の参照パスに一致させる）。
- 決定: DPI プリセット定数は core/render_size.hpp に一元化する。
- Task 2: 規約スキャナ `tst_no_hardcode` を追加。`tests/CMakeLists.txt` に `utsushi_add_test(name)` ヘルパを定義（`qt_add_executable` + `Qt6::Test` リンク + `add_test` 登録。以後のテストは 1 行追加のみで済む）。
- `tests/tst_no_hardcode.cpp` は core/app/tests 配下の `.cpp`/`.hpp` を正規表現で走査し、QtWidgets/QMessageBox/qDebug の core/ への混入、マジックナンバー 72 の render_size.*以外への出現、core/ の生 `new`、Qt5 API（QRegExp/SIGNAL()/SLOT()/foreach()/qrand()/QString::null）、例外（throw/try/catch）と processEvents の使用を検出する。
- RED 確認: `core/probe.hpp` に `#include <QtWidgets/QLabel>` を仕込んで `ctest` が FAIL することを確認済み。削除後は PASS に復帰。core/ はまだ存在しないため、通常時は走査対象 0 件で PASS するのが正しい状態（Task 3 で core/ 追加後に実質的な監視が始まる）。
