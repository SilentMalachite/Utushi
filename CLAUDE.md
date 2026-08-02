# CLAUDE.md — Utsushi (PDF → PNG 変換アプリ)

> このファイルは Claude Code への恒久指示。実装の詳細な流儀は `docs/qt-conventions.md` に分離する。
> レビュー担当（Codex / GPT-Sol）向けの契約は `AGENTS.md`。

---

## 結論

Qt 6.8 以上（開発機・CI は 6.11.x）/ C++20 / CMake で、PDF を PNG 連番画像に変換するデスクトップアプリ **Utsushi（写し）** を作る。
PDF のラスタライズは **Qt 標準の QtPdf モジュール（`QPdfDocument`）** のみを使い、Poppler・MuPDF・外部プロセスには依存しない。
GUI は Qt Widgets。macOS (arm64) と Windows (x64) の2プラットフォームを対象とする。

---

## 前提

- 開発者は 1 名。AI との協働開発。C++ は AI 補助前提で書くため、**規約違反を CI で機械的に落とす**方針を取る。
- ネットワーク通信は行わない。テレメトリ・自動更新・クラッシュレポータをすべて持たない完全オフラインアプリ。
- 入力 PDF はローカルファイルのみ。出力は同一マシンのローカルディレクトリのみ。
- Qt は **LGPLv3 で動的リンク**して使う。静的リンク・Qt ソースの改変は行わない（ライセンス上の重大な制約。`docs/qt-conventions.md` のライセンス節を参照）。

---

## スコープ

### やること（v0.1.0）

1. PDF ファイルを開く（単一ファイル / 複数ファイルのバッチ）
2. ページ範囲指定（全ページ / `1-5,8,11-` 形式）
3. 出力解像度指定（DPI: 72 / 150 / 300 / 600 / 任意値）
4. 出力先ディレクトリ指定
5. PNG 連番出力（命名規則は「仕様」参照）
6. 進捗表示とキャンセル
7. 変換結果のサマリ表示（成功件数 / 失敗件数 / 失敗理由）

### やらないこと（明示的な非スコープ）

- PDF の編集・結合・分割
- OCR、テキスト抽出
- PNG 以外の出力形式（JPEG/TIFF/WebP は v0.2 以降で検討）
- クラウド連携、自動更新、テレメトリ
- Linux 対応（動くようには書くが、CI では検証しない）

---

## 仕様

### アーキテクチャ

```
utsushi/
├── CMakeLists.txt
├── CLAUDE.md
├── AGENTS.md
├── docs/
│   └── qt-conventions.md
├── core/                    # ← Qt Widgets に依存しない。Core + Gui + Pdf のみ
│   ├── CMakeLists.txt
│   ├── page_range.hpp/.cpp      # 純粋関数: "1-5,8" → std::vector<int>
│   ├── render_size.hpp/.cpp     # 純粋関数: (pagePointSize, dpi) → QSize
│   ├── output_path.hpp/.cpp     # 純粋関数: (stem, index, total) → ファイル名
│   ├── conversion_job.hpp/.cpp  # 1 変換ジョブの値オブジェクト（不変）
│   └── converter.hpp/.cpp       # QObject。ジョブ実行とシグナル発行
├── app/                     # ← GUI。core に依存する。core は app に依存しない
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── main_window.hpp/.cpp
│   └── ...
├── tests/
│   ├── CMakeLists.txt
│   ├── tst_page_range.cpp
│   ├── tst_render_size.cpp
│   ├── tst_output_path.cpp
│   ├── tst_converter.cpp        # 検証用 PDF は QPdfWriter で実行時に合成する（バイナリを
│   │                              git に置かず、Qt バージョン間の差異でも壊れないため）
│   ├── tst_main_window.cpp      # app/ を組み込む唯一のテスト。Qt6::Widgets をリンクする
│   └── tst_no_hardcode.cpp      # 規約スキャナ（不変条件テスト）
└── packaging/
    ├── macos/
    └── windows/
```

**依存の向きは一方向。`core/` から `app/` のヘッダを include したら即座に設計エラー。**

### 計算はすべて純粋関数に切り出す

I/O（ファイル読み書き、`QPdfDocument` の呼び出し）と、計算（ページ範囲の解釈、サイズ算出、ファイル名生成）を混ぜない。
計算部分は `QObject` を継承せず、副作用を持たず、テストから直接呼べる自由関数にする。

```cpp
// core/render_size.hpp
namespace utsushi {
// PDF のページサイズ（ポイント, 1pt = 1/72 inch）と DPI から出力ピクセルサイズを求める。
// 副作用なし。例外を投げない。dpi <= 0 のとき std::nullopt。
[[nodiscard]] std::optional<QSize> renderSizeFor(QSizeF pagePointSize, double dpi) noexcept;
}
```

`px = round(points / 72.0 * dpi)`。幅・高さとも最低 1px を保証する。
上限は 1 辺 20000px。超える場合は `std::nullopt` を返し、UI 側で「DPI が大きすぎます」と伝える。

### 出力ファイル命名規則

```
{入力PDFのstem}_p{ページ番号をゼロ埋め}.png
例: report.pdf の 7 ページ目（全 120 ページ）→ report_p007.png
```

- ゼロ埋め桁数は **総ページ数の桁数**（120 ページなら 3 桁）。ただし最低 3 桁。
- ページ番号は 1 始まり（PDF ビューアの表示と一致させる。内部の 0 始まりインデックスと混同しない）。
- **既存ファイルを黙って上書きしない。** 既存ファイルがある場合の挙動はユーザーが明示的に選ぶ（`Skip` / `Overwrite` / `Rename(_2 を付与)`）。既定は `Skip`。
- 出力先が書き込み不可・容量不足の場合は、変換開始前に検出して中止する。

### レンダリング

- `QPdfDocument::render(page, size, options)` を使う。
- **`QPdfDocument` はスレッドセーフではない前提で扱う。** 変換はワーカースレッド 1 本で行い、そのスレッドが自前の `QPdfDocument` インスタンスを所有する。UI スレッドの `QPdfDocument` を共有しない。
- 並列化は v0.1 では行わない（ファイル単位の並列は v0.2 以降で検討）。
- 進捗・完了・失敗は `Converter` の signal で UI に伝える。UI から core を直接ポーリングしない。
- キャンセルは `std::atomic<bool>` フラグをワーカーがページ境界で確認する方式。スレッドの強制終了はしない。

### エラー処理

`QPdfDocument::Error` を握り潰さない。以下をすべてユーザーに識別可能な形で提示する。

| 状況 | 挙動 |
|---|---|
| ファイルが存在しない / 読めない | そのファイルを失敗として記録し、次のファイルへ進む |
| PDF として不正 | 同上 |
| パスワード保護 | ダイアログでパスワードを 1 回だけ問う。キャンセルなら失敗として記録 |
| 特定ページのみレンダリング失敗 | そのページを失敗として記録し、次のページへ進む |
| 出力先に書き込めない | 変換全体を中止し、理由を表示する |

**バッチ処理は 1 件の失敗で全体を止めない。** 最後にサマリで失敗一覧を示す。

---

## TDD

**実装コードより先にテストを書く。例外なし。**

1. 純粋関数（`page_range` / `render_size` / `output_path`）は Qt Test のデータ駆動テスト（`_data()` + `QFETCH`）で境界値を網羅する。
   - `page_range`: `""`, `"1"`, `"1-5"`, `"1-5,8"`, `"11-"`, `"5-1"`(逆順), `"0"`, `"abc"`, 総ページ数超過, 重複指定
   - `render_size`: dpi = 0 / 負 / 72 / 300 / 巨大値、ページサイズが 0 のとき
   - `output_path`: 総ページ数 1 / 9 / 10 / 999 / 1000、stem に `/` や日本語や空白を含むとき
2. `converter` は `tests/fixtures/` の小さな PDF（2〜3 ページ、1 ページは意図的に壊す）に対して統合テストを書く。出力 PNG の**サイズとページ数**を検証する（ピクセル完全一致のゴールデン比較はしない。Qt のバージョン差で壊れるため）。
3. `tst_no_hardcode.cpp` を不変条件テストとして維持する。以下を検出したら **失敗させる**：
   - `core/` 配下に `#include <QtWidgets` が現れる
   - `QMessageBox` / `qDebug` が `core/` に現れる
   - マジックナンバー `72`（DPI 換算定数）が `render_size.cpp` 以外に現れる
   - `new` の直後に `delete` を伴わない生ポインタ（親を持たない `QObject` 生成）

テストが通らないコードはコミットしない。

---

## 制約

### C++ / Qt

詳細は `docs/qt-conventions.md`。**必ず読んでから書くこと。** 要点だけ再掲する。

- **C++20 を使う。C++11/14 時代の書き方をしない。**
- **Qt5 の API・スタイルを混ぜない。** `QRegExp` / `qrand()` / `SIGNAL()`/`SLOT()` マクロ / `QString::null` / `foreach` は禁止。Qt6 の `QRegularExpression`、関数ポインタ版 `connect`、範囲 for を使う。
- **`QtWidgets` を `core/` に入れない。**
- 生ポインタ `new` は「Qt の親子関係で所有権を渡す場合」だけ。それ以外は `std::unique_ptr`。
- `QString` と `std::string` を混在させない。UI に出る文字列はすべて `QString` + `tr()`。
- 例外を投げない。失敗は `std::optional` / `std::expected` 相当の戻り値で表す。
- ファイルパスは `QString` ではなく可能な限り `QFileInfo` / `QDir` を通す。Windows のパス区切りとドライブレター、macOS の NFD 正規化差異を自前で処理しない。

### プロセス

- **1 コミット = 1 つの改善。** 差分は最小に保つ。
- 「ついでのリファクタリング」を混ぜない。気づいた改善点は TODO として別ファイルに書き出し、次のコミットで扱う。
- 進捗と決定事項は **`docs/progress.md` に外部ファイルとして書き出す。** 会話の中だけで完結させない。
- ライブラリ追加は事前に相談する。既定の依存は Qt6 のみ。

### アクセシビリティ（この開発者にとっての必須要件）

- アニメーション・フェード・スライドを使わない。状態遷移は即時。
- 進捗はプログレスバーだけでなく **テキストでも** 示す（「120 ページ中 37 ページ完了」）。
- すべての操作がキーボードだけで完結する。タブ順序を明示的に設定する。
- ダークモードでコントラストが破綻しないこと。色だけで状態を区別しない（成功/失敗はアイコンと文言でも区別）。
- 長時間の変換中に UI がフリーズしないこと（レンダリングは必ずワーカースレッド）。

---

## 受け入れ基準（v0.1.0）

以下がすべて満たされたときに v0.1.0 とする。

- [ ] macOS (arm64) と Windows (x64) の GitHub Actions で、ビルドと `ctest` が緑
- [ ] 100 ページの PDF を 300 DPI で変換中、UI が応答し続ける
- [ ] 変換中にキャンセルすると 1 秒以内にワーカーが停止し、途中までの PNG は残る
- [ ] 壊れた PDF を含む 3 ファイルのバッチで、正常 2 件が変換され、失敗 1 件がサマリに理由付きで表示される
- [ ] 既存の出力ファイルが `Skip` 設定で上書きされない
- [ ] `tst_no_hardcode` が緑
- [ ] `macdeployqt` / `windeployqt` を通した配布物が、Qt 未インストールのクリーンな環境で起動する
- [ ] Qt を動的リンクしており、`ヘルプ > ライセンス` から LGPLv3 の告知と Qt のソース入手先を表示できる

---

## ビルド

```bash
# 設定
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$QT_ROOT"

# ビルド
cmake --build build --parallel

# テスト
ctest --test-dir build --output-on-failure
```

Qt PDF は Qt のアドオンモジュール。Qt Maintenance Tool / aqtinstall で **`qtpdf` を明示的に選択**しないと `find_package(Qt6 COMPONENTS Pdf)` が失敗する。CI の設定でも同様。
