# Utsushi 進捗ログ

## 2026-08-02
- リポジトリ初期化。CMake スケルトンと空ウィンドウの app を作成。
- 決定: qt-conventions.md を docs/ へ移動（CLAUDE.md の参照パスに一致させる）。
- 決定: DPI プリセット定数は core/render_size.hpp に一元化する。
- Task 2: 規約スキャナ `tst_no_hardcode` を追加。`tests/CMakeLists.txt` に `utsushi_add_test(name)` ヘルパを定義（`qt_add_executable` + `Qt6::Test` リンク + `add_test` 登録。以後のテストは 1 行追加のみで済む）。
- `tests/tst_no_hardcode.cpp` は core/app/tests 配下の `.cpp`/`.hpp` を正規表現で走査し、QtWidgets/QMessageBox/qDebug の core/ への混入、マジックナンバー 72 の render_size.*以外への出現、core/ の生 `new`、Qt5 API（QRegExp/SIGNAL()/SLOT()/foreach()/qrand()/QString::null）、例外（throw/try/catch）と processEvents の使用を検出する。
- RED 確認: `core/probe.hpp` に `#include <QtWidgets/QLabel>` を仕込んで `ctest` が FAIL することを確認済み。削除後は PASS に復帰。core/ はまだ存在しないため、通常時は走査対象 0 件で PASS するのが正しい状態（Task 3 で core/ 追加後に実質的な監視が始まる）。
- Task 3: `core/` ライブラリを新設し、最初の純粋関数 `utsushi::parsePageRange(const QString& spec, int totalPages)`（`core/page_range.hpp` / `.cpp`）を追加。ページ範囲指定文字列（`"1-5,8,11-"` 形式）を 1 始まり・昇順・重複なしの `std::vector<int>` に解釈する。不正入力（逆順・0 以下・総ページ数超過・数値以外・空トークン・`totalPages <= 0`）は `std::nullopt`。
  - `core/CMakeLists.txt` で `utsushi_core` STATIC ライブラリを定義（`Qt6::Core`/`Qt6::Gui`/`Qt6::Pdf` にリンク）。ルート `CMakeLists.txt` に `add_subdirectory(core)` を `add_subdirectory(app)` の前に追加。
  - `tests/CMakeLists.txt` の `utsushi_add_test` ヘルパを `utsushi_core` リンク版に更新し、`tst_page_range` を登録。
  - TDD: `core/page_range.cpp` を `return std::nullopt;` のみのスタブにして `ctest -R page_range` が 9/17 行で FAIL することを確認（RED）してから本実装に差し替え、全 17 行 PASS を確認（GREEN）。
  - 逸脱: ブリーフのコードには無い `#include <QList>` を `page_range.cpp` に追加した。開発機の Qt 6.11.1 では `QStringView::split()` が返す `QList<QStringView>` が `<QString>` からの透過的インクルードだけでは前方宣言止まりで、range-based for がコンパイルエラーになったため（`qcontainerfwd.h` の前方宣言のみでは実体が要る用途に不足）。ロジックはブリーフどおり無変更。
  - `tst_no_hardcode` は今回初めて `core/` に実体のあるコードを走査し、5 スロットすべて green（QtWidgets/QMessageBox/qDebug 不在、マジックナンバー 72 不在、生 `new` 不在、Qt5 API 不在、例外/processEvents 不在）。
