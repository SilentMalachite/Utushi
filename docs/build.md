# docs/build.md — ビルド・テスト・配布物

Utsushi をソースからビルドし、テストと静的解析を実行し、配布可能な形にまとめるまでの手順。

実装時の書き方の規約は [qt-conventions.md](qt-conventions.md)、レビュー時の検証手順は [../AGENTS.md](../AGENTS.md) にあります。

---

## 最初に: Qt の `qtpdf` モジュールを明示的に入れる

**ここが唯一かつ最大の落とし穴です。** Qt PDF は Qt のアドオンモジュールで、既定では入りません。入れ忘れると configure が次で止まります。

```
CMake Error: Found package configuration file ... but it set Qt6_FOUND to FALSE
  so package "Qt6" is considered to be NOT FOUND. Reason given by package:
  Failed to find required Qt component "Pdf".
```

インストール方法に応じて次のいずれかで入れてください。

**Qt Online Installer / Maintenance Tool**
対象バージョンの下にある **Additional Libraries > Qt PDF** にチェックを入れる。

**aqtinstall**

```bash
# macOS (Apple Silicon)
aqt install-qt mac desktop 6.11.1 clang_64 -m qtpdf

# Windows (MSVC 2022 x64)
aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -m qtpdf
```

CI も同じ理由で `modules: 'qtpdf'` を明示しています（`.github/workflows/ci.yml`）。

> Windows で 6.11.x を指定すると `aqt` が失敗します。上流が Windows 向けバイナリを公開していないためです。詳細と現在の回避策は [known-issues.md](known-issues.md#windows-向け-qt-611x-のバイナリが上流に存在しない) を参照してください。

---

## 必要なもの

| 項目 | 要件 |
| --- | --- |
| Qt | 6.8 以上（`Core` / `Gui` / `Widgets` / `Pdf` / `Test`）。開発機は 6.11.1 |
| CMake | 3.21 以上 |
| コンパイラ | C++20 対応（macOS: Apple Clang、Windows: MSVC 2022） |
| clang-tidy | 静的解析を回す場合のみ。macOS では Homebrew LLVM 版が必要（後述） |

依存は Qt6 のみです。追加のサードパーティライブラリはありません。

---

## ビルド

```bash
# 設定
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$QT_ROOT"

# ビルド
cmake --build build --parallel
```

`$QT_ROOT` は Qt のプレフィックス（例: `/Users/hiro/Qt/6.11.1/macos`、`C:/Qt/6.10.3/msvc2022_64`）。`CMAKE_PREFIX_PATH` を渡さなくても Qt が見つかる環境ではその指定を省略できます。

Windows で Visual Studio ジェネレータを使う場合は、CI と同じ指定にしておくと成果物のパスも一致します。

```bash
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --parallel --config Release
```

### `UTSUSHI_WERROR`

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DUTSUSHI_WERROR=ON
```

警告をエラーとして扱います（GCC/Clang は `-Wall -Wextra -Werror`、MSVC は `/W4 /WX`）。既定は `OFF` ですが、**CI は Release + `UTSUSHI_WERROR=ON` でビルドします。** Debug かつ WERROR なしのローカルビルドだけでは通ってしまう警告が実際にありました（`[[nodiscard]]` の戻り値無視）。コミット前に一度はこの組み合わせを通してください。

---

## テスト

```bash
ctest --test-dir build --output-on-failure
```

Visual Studio ジェネレータなどのマルチコンフィグ環境では設定名が要ります。

```bash
ctest --test-dir build -C Release --output-on-failure
```

登録されているスイートは 6 本です。

| スイート | 対象 |
| --- | --- |
| `tst_page_range` | ページ範囲指定の解釈（データ駆動、境界値） |
| `tst_render_size` | DPI とページサイズから出力ピクセルサイズへの換算 |
| `tst_output_path` | 出力ファイル名の生成と stem のサニタイズ |
| `tst_converter` | 変換ジョブの統合テスト（21 ケース） |
| `tst_main_window` | GUI の壊れやすい契約。`app/` を組み込む唯一のテスト |
| `tst_no_hardcode` | 規約スキャナ。ソースを走査して規約違反を検出する不変条件テスト |

**検証用の PDF はリポジトリに置いていません。** `tst_converter` は `QPdfWriter` で実行時に合成します（バイナリを git に入れず、Qt のバージョン差でも壊れないようにするため）。`tests/fixtures/` は存在しません。

### ヘッドレス環境で実行する場合

`tst_converter` は `QGuiApplication` を、`tst_main_window` は `QApplication` を必要とします。ディスプレイのない環境ではオフスクリーンプラグインを指定してください。

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

CI ではジョブ全体の環境変数として設定しています。

### 規約スキャナだけを回す

```bash
ctest --test-dir build -R no_hardcode --output-on-failure
```

`tst_no_hardcode` は `core/` `app/` `tests/` の `.cpp` / `.hpp` を走査し、次を検出すると失敗します。

- `core/` に `QtWidgets` / `QMessageBox` / `qDebug` が混入している
- DPI 換算のリテラル `72` が `render_size.hpp` / `render_size.cpp` 以外に現れている
- `core/` に親を持たない生の `new` がある
- Qt5 時代の API（`QRegExp` / `SIGNAL()` / `SLOT()` / `foreach` / `qrand` / `QString::null`）を使っている
- 例外（`throw` / `try` / `catch`）や `processEvents` を使っている

---

## clang-tidy による静的解析

CI には含めず、ローカルでのゲートとして運用しています（理由は [AGENTS.md](../AGENTS.md) 参照）。チェックセットはリポジトリルートの `.clang-tidy` に定義済みなので `-checks=` の指定は不要です。

```bash
cmake -B build-tidy -S . -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DCMAKE_PREFIX_PATH="$QT_ROOT"

/opt/homebrew/opt/llvm/bin/clang-tidy -p build-tidy \
    --extra-arg="-isysroot$(xcrun --show-sdk-path)" --extra-arg=-stdlib=libc++ \
    core/*.cpp app/*.cpp
```

注意点が 3 つあります。

1. **`build/` とは別に `build-tidy/` を用意する。** `compile_commands.json` の出力に `CMAKE_EXPORT_COMPILE_COMMANDS=ON` が要るためです。
2. **Homebrew LLVM の clang-tidy を使う。** Xcode 付属の clang には clang-tidy が同梱されておらず代替できません。`$PATH` にも入っていないのでフルパスで呼びます。
3. **`-isysroot` / `-stdlib=libc++` を省略しない。** 省略すると Homebrew の clang-tidy が macOS SDK のヘッダを見つけられず、`'type_traits' file not found` のような `clang-diagnostic-error` を大量に出したまま**ほとんど解析せずに exit code 0 で終了します**。中断であって成功ではありません。

そのため「診断 0 件」は、出力に `clang-diagnostic-error` が 1 件もないことを合わせて確認して初めて意味を持ちます。

```bash
... | tee tidy.log; grep -c clang-diagnostic-error tidy.log   # 0 であること
```

上記は macOS（Apple Clang SDK）向けの手順です。Windows / MSVC では未検証です。

---

## 配布物を作る

### ビルド成果物のパスを推測しない

成果物の実パスはプラットフォームとジェネレータで変わります（Windows のマルチコンフィグでは `qt_standard_project_setup()` が DLL 同居のため全実行ファイルを `build/<Config>/` に平坦化するので、`build/app/Release/` にはなりません）。

`app/CMakeLists.txt` が configure 時に実パスを書き出しているので、それを読んでください。

```bash
cat build/utsushi_layout_Release.txt
# EXECUTABLE=...      実行ファイル本体
# DEPLOY_TARGET=...   macdeployqt / windeployqt に渡す対象
# ARTIFACT_DIR=...    配布物のルート
```

### macOS

```bash
cmake --build build --config Release
macdeployqt "$(grep '^DEPLOY_TARGET=' build/utsushi_layout_Release.txt | cut -d= -f2-)" -verbose=1
```

`utsushi.app/Contents/Frameworks/` に Qt フレームワークが動的リンクで配置され、`Contents/Resources/LICENSE` が同梱されます（LICENSE のコピーは `POST_BUILD` で行われるため、`cmake --install` は不要です）。

### Windows

テストバイナリ（`tst_*.exe`）が実行ファイルと同じディレクトリに平坦化されるため、**そのディレクトリをそのまま配布物にしないでください。** 出荷するものだけを別ディレクトリへ積み、そこへ `windeployqt` を実行します。

```bash
mkdir -p dist/windows
cp "$EXECUTABLE" dist/windows/
cp "$ARTIFACT_DIR/LICENSE" dist/windows/
windeployqt dist/windows/utsushi.exe
```

### 署名・公証

行っていません。配布物は未署名です（[known-issues.md](known-issues.md#配布物に署名や公証をしていない)）。

---

## CI が検証していること

`.github/workflows/ci.yml` は `macos-14`（arm64 / `clang_64` / Qt 6.11.x）と `windows-2022`（x64 / `win64_msvc2022_64` / Qt 6.10.x）のマトリクスで、push と pull request のたびに次を実行します。

1. Release + `UTSUSHI_WERROR=ON` でのビルド
2. `ctest`（6 スイート）
3. `macdeployqt` / `windeployqt` による配布物の生成
4. 配布物の静的検証
   - 実行ファイルが生成されていること
   - `LICENSE` が同梱されていること
   - Qt フレームワーク（macOS）/ Qt DLL（Windows）が配置されていること
   - 外部の Qt / Homebrew への参照が残っていないこと（macOS）
   - 不足している DLL 依存がないこと（Windows）
   - テストバイナリが配布物に混入していないこと（Windows）
5. **Qt を環境から取り除いた状態で実際に起動すること**（両 OS）
6. アーティファクトのアップロード（`utsushi-macos-arm64` / `utsushi-windows-x64`）

CI に含めていないもの: clang-tidy（上記の理由）、GUI の目視確認（[known-issues.md](known-issues.md#windows-での-gui-確認が未実施)）。

---

## リポジトリ構成

```
core/     Qt Widgets に依存しない計算と変換処理（Core + Gui + Pdf のみ）
app/      GUI。core に依存する
tests/    Qt Test のテスト。tst_main_window だけが app/ を組み込む
packaging/  将来の Info.plist / entitlements / アイコン置き場（現在は空）
```

**依存の向きは一方向です。`core/` から `app/` のヘッダを include したら設計エラーで、`tst_no_hardcode` が落ちます。**

`build/` と `build-*/` は `.gitignore` 済みです。

---

## うまくいかないとき

| 症状 | 原因と対処 |
| --- | --- |
| configure が `Failed to find required Qt component "Pdf"` で止まる | `qtpdf` モジュールが入っていません。冒頭の手順で追加してください |
| configure が Qt6 を見つけられない | `-DCMAKE_PREFIX_PATH="$QT_ROOT"` を渡してください |
| ヘッドレス環境でテストが起動時に落ちる | `QT_QPA_PLATFORM=offscreen` を付けてください |
| clang-tidy が `'type_traits' file not found` を大量に出す | `-isysroot` / `-stdlib=libc++` を省略しています。上記の手順どおりに |
| clang-tidy が「診断 0 件」で終わるが解析された気がしない | `clang-diagnostic-error` の有無を確認してください。中断でも exit code 0 になります |
| Windows で `aqt` が Qt 6.11.x を入れられない | 上流にバイナリがありません。6.10.x を使ってください（[known-issues.md](known-issues.md#windows-向け-qt-611x-のバイナリが上流に存在しない)） |
| CI は緑なのにローカルで警告が出る／逆 | ビルド種別が違います。CI は Release + `UTSUSHI_WERROR=ON` です |
