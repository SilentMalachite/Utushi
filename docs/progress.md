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
- Task 4: 2 つ目の純粋関数 `utsushi::renderSizeFor(QSizeF pagePointSize, double dpi) noexcept`（`core/render_size.hpp` / `.cpp`）を追加。PDF ページのポイントサイズと DPI から出力ピクセルサイズ（`std::optional<QSize>`）を求める。`px = round(points / 72.0 * dpi)`、幅・高さとも最低 1px 保証、1 辺が `kMaxRenderEdgePx`（20000px）超か `dpi <= 0` かページ辺が 0 以下なら `std::nullopt`。
  - DPI 換算定数 `utsushi::kScreenDpi = 72` と UI プリセット `utsushi::kStandardDpiPresets = {72, 150, 300, 600}` を `render_size.hpp` に一元化（`tst_no_hardcode::magic72OnlyInRenderSize` が監視。マジックナンバー 72 はこのファイルと `render_size.cpp` にのみ書ける）。Task 7（Converter）と Task 8（DPI コンボ）が両定数と `renderSizeFor` を消費する想定。
  - `core/CMakeLists.txt` のソース一覧に `render_size.hpp render_size.cpp` を追加。`tests/CMakeLists.txt` に `utsushi_add_test(tst_render_size)` を追加。
  - TDD: `core/render_size.cpp` を `return std::nullopt;` のみのスタブにして `ctest -R render_size` が 8 passed / 6 failed（`valid=true` の行のみ FAIL）で RED を確認してから本実装に差し替え、14/14 PASS を確認（GREEN）。
  - A4（595.276×841.890pt）@72/150/300/600 DPI の期待値（595×842 / 1240×1754 / 2480×3508 / 4961×7016）はブリーフどおり検算一致。編集なし。
  - 逸脱なし。ブリーフのコード（テスト・ヘッダ・実装）をそのまま転記した。
  - `tst_no_hardcode` は 5 スロット全 green を維持（`magic72OnlyInRenderSize` を含む）。
- Task 5: 3 つ目の純粋関数群 `utsushi::sanitizedStem(const QString&)` と `utsushi::outputFileName(const QString&, int pageNumber, int totalPages)`（`core/output_path.hpp` / `.cpp`）を追加。Windows の使用禁止文字（`\ / : * ? " < > |`）と制御文字（`\x00`-`\x1f`）を `_` に置換し、NFC 正規化・前後トリムした上で空なら `"output"` を返す `sanitizedStem`、および `"{stem}_p{ゼロ埋めページ番号}.png"` を組み立てる `outputFileName` を実装。ゼロ埋め桁数は総ページ数の桁数（`QString::number(totalPages).size()`）、ただし最低 3 桁。
  - `core/CMakeLists.txt` のソース一覧に `output_path.hpp output_path.cpp` を追加。`tests/CMakeLists.txt` に `utsushi_add_test(tst_output_path)` を追加。
  - TDD: `core/output_path.cpp` を `sanitizedStem`/`outputFileName` とも `return QString{};` のみのスタブにして `ctest -R output_path` が 2 passed（`initTestCase`/`cleanupTestCase`）/ 17 failed（データ行すべて）で RED を確認してから本実装に差し替え、`tst_output_path` 19/19 PASS（GREEN）。4 スイート合計（`tst_no_hardcode`/`tst_page_range`/`tst_render_size`/`tst_output_path`）も全 PASS。
  - 境界値（total=9→3桁, total=10→3桁, total=999→3桁, total=1000→4桁）はブリーフどおり検算一致。
  - NFD→NFC 行（`stem(nfd-to-nfc)`）の transcribe が単なる恒真式（同一リテラルの比較）に劣化していないことを、ソースファイルとコンパイル後バイナリの両方で確認した: 入力側は 2 コードポイント（U+30DB カタカナ「ホ」+ U+309A 結合半濁点、NFD 分解形）、期待値側は 1 コードポイント（U+30DD カタカナ「ポ」、NFC 合成形）であり、`tests/tst_output_path.cpp` を Python で codepoint 単位に読み直して確認、さらに `build/tests/tst_output_path` バイナリ中に両方の UTF-16LE バイト列（`db309a30` = NFD 2 code units、`dd30` = NFC 1 code unit）が別個に存在することをバイナリ検索で確認した。テストは `sanitizedStem` が NFD 入力を NFC 期待値へ正しく正規化することを検証して PASS。
  - 逸脱なし。ブリーフのコード（テスト・ヘッダ・実装）をそのまま転記した。Task 3 で発生した `<QList>` 不完全型エラーは today の Qt 6.11.1 環境では再発せず（`output_path.cpp` は追加インクルードなしでビルド成功）。
  - `tst_no_hardcode` は 5 スロット全 green を維持。`core/output_path.cpp` の `QStringLiteral(R"([\\/:*?"<>|]|[\x00-\x1f])")` は生文字列リテラルだが、内容に禁止トークン（`new`/`72`/`QRegExp`/`throw` 等）を含まないため、スキャナが生文字列を特別扱いしない制限（`tst_no_hardcode.cpp` のコメント参照）による誤検知は発生しない。
