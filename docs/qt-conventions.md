# docs/qt-conventions.md — C++20 / Qt6 実装規約

> `CLAUDE.md` から参照される。実装を書く前に必ず読む。
> **この文書の目的は「AI が学習データ中の古い Qt5 / C++11 コードに引きずられるのを防ぐこと」。**

---

## 結論

**Qt 6.8 LTS / C++20 のみを書く。Qt5 の API、C++11 時代のイディオムは 1 行も混ぜない。**
C++ は 40 年分の書き方が積層しており、AI が生成するコードは高い確率で古い層に落ちる。下記の禁止リストを機械的に適用する。

---

## 前提

- コンパイラ: Apple Clang (macOS) / MSVC 2022 (Windows)
- 標準: C++20（`CMAKE_CXX_STANDARD 20`, `CXX_STANDARD_REQUIRED ON`, `CXX_EXTENSIONS OFF`）
- Qt: 6.8 LTS。使用モジュールは `Core` `Gui` `Widgets` `Pdf` `Test` のみ

---

## 禁止と代替（Qt5 → Qt6）

| 禁止 | 代替 |
|---|---|
| `QRegExp` | `QRegularExpression` |
| `SIGNAL()` / `SLOT()` マクロ | 関数ポインタ版 `connect(sender, &Sender::sig, receiver, &Receiver::slot)` |
| `foreach` / `Q_FOREACH` | 範囲 for。コンテナは `const auto&` で受ける |
| `qrand()` / `qsrand()` | `QRandomGenerator` |
| `QString::null` | `QString{}` |
| `QVariant::type()` | `QVariant::typeId()` / `metaType()` |
| `QDesktopWidget` | `QScreen` |
| `QTextStream::setCodec()` | `setEncoding(QStringConverter::Utf8)` |
| `.toStdString()` を経由した文字列処理 | `QString` のまま処理する |
| `QMutexLocker locker(&m)`（テンプレート引数なし） | `QMutexLocker<QMutex> locker(&m)` |

**判断に迷ったら Qt6 のドキュメントを確認する。記憶で書かない。** Qt5 と Qt6 でシグネチャが変わった API は多い。

---

## 禁止と代替（C++11/14 → C++20）

| 禁止 | 代替 |
|---|---|
| 生ポインタの `new` / `delete` | `std::unique_ptr`。ただし Qt の親子関係で所有権を渡す場合のみ生 `new` 可 |
| `typedef` | `using` |
| `NULL` / `0` | `nullptr` |
| 出力引数（`bool f(int&out)`） | 戻り値。複数値は構造体か `std::pair` |
| `enum` | `enum class` |
| 生の `for (int i = 0; ...)` によるコンテナ走査 | 範囲 for、`std::ranges` |
| マクロによる定数 | `constexpr` |
| 実行時に決まらない条件分岐 | `if constexpr` |

- `[[nodiscard]]` を、失敗を返しうるすべての関数に付ける。
- `noexcept` を、投げないと保証できる関数に付ける。**本プロジェクトは例外を使わないので、ほぼすべての純粋関数に付く。**
- `const` を既定にする。非 `const` は理由があるときだけ。

---

## 所有権とメモリ

Qt の親子所有権と `std::unique_ptr` を**混ぜない**。どちらか一方に決める。

```cpp
// ✅ 正: 親を持つ QObject は生 new でよい。親が delete する。
auto* button = new QPushButton(tr("変換"), this);   // this が親

// ✅ 正: 親を持たないものは unique_ptr
std::unique_ptr<QPdfDocument> doc = std::make_unique<QPdfDocument>();

// ❌ 誤: 親を持つ QObject を unique_ptr で持つ（二重 delete）
std::unique_ptr<QPushButton> button = std::make_unique<QPushButton>(tr("変換"), this);
```

`QObject` を `unique_ptr` で持つときは **親を渡さない**。これが唯一の規則。

---

## スレッド

**本プロジェクトで最も事故が起きやすい箇所。**

- `QPdfDocument` は**スレッドセーフでない前提で扱う**。1 つのインスタンスは 1 つのスレッドだけが触る。
- ワーカースレッドは `QThread` を継承しない。`QObject` をワーカーにして `moveToThread()` する（Qt 公式の推奨パターン）。
- ワーカーから UI を直接触らない。**必ず signal 経由**。`QLabel::setText()` をワーカースレッドから呼んだら即バグ。
- スレッド間の値の受け渡しは、コピー可能な値オブジェクトで行う。ポインタを渡さない。
- キャンセルは `std::atomic<bool>`。`QThread::terminate()` は絶対に呼ばない。
- `QCoreApplication::processEvents()` を UI のフリーズ回避に使わない。**これは解決ではなく隠蔽であり、再入バグを生む。**

```cpp
// ワーカー側: ページ境界でキャンセルを確認する
for (int i = 0; i < pages.size(); ++i) {
    if (m_cancelRequested.load(std::memory_order_relaxed)) {
        emit cancelled(i);
        return;
    }
    // ... render & save ...
    emit pageDone(i + 1, pages.size());   // 1 始まりで UI に渡す
}
```

---

## エラー処理

- 例外を投げない。`throw` / `try` / `catch` を書かない。
- 失敗しうる関数は `std::optional<T>` を返す。失敗理由が必要なら `struct Result { bool ok; QString message; }` のような明示的な型を返す。
- **戻り値を無視しない。** `QImage::save()` は `bool` を返す。無視した瞬間に「変換したはずのファイルが無い」バグになる。
- `Q_UNUSED` でコンパイル警告を黙らせない。使わない引数があるなら、そもそも引数が不要か設計を疑う。

---

## プラットフォーム差異

| 論点 | 方針 |
|---|---|
| パス区切り | 自前で `/` や `\` を扱わない。`QDir` / `QFileInfo` / `QDir::toNativeSeparators()` に任せる |
| ファイル名の使用禁止文字 | Windows で `\ / : * ? " < > \|` が使えない。PDF の stem をそのまま出力名に使う前に**サニタイズする関数を経由する** |
| macOS のファイル名正規化 | HFS+/APFS は NFD。日本語ファイル名の比較で NFC/NFD 差が出る。比較は `QString::normalized(QString::NormalizationForm_C)` を通す |
| 高 DPI | `Qt::AA_EnableHighDpiScaling` は Qt6 では既定で有効。明示的に設定しない |
| macOS のメニューバー | `QMenuBar` はネイティブメニューに移る。「終了」「環境設定」の項目は自動で移動するので二重に置かない |

---

## ライセンス（重大な制約）

- Qt を **LGPLv3 で使う**。したがって **Qt は動的リンクする**。静的リンクは商用ライセンスがないと配布できない。
- `windeployqt` / `macdeployqt` が生成する DLL / dylib をそのまま同梱する。バイナリを結合・難読化しない。
- Qt のソースコードを改変しない。改変した場合は改変版ソースの公開義務が生じる。
- アプリ内に `ヘルプ > ライセンス` を設け、以下を表示する:
  - 本アプリのライセンス（GPLv3）
  - Qt が LGPLv3 であること、および Qt のソース入手先 URL
  - Qt PDF が内部で PDFium（BSD-3-Clause）を使っていること
- **この節に関わる変更は AI の判断で行わない。必ず開発者に確認する。**

---

## CMake

```cmake
cmake_minimum_required(VERSION 3.21)
project(utsushi LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Pdf Test)
qt_standard_project_setup()

enable_testing()
add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)
```

- `qt_standard_project_setup()` を使う（`CMAKE_AUTOMOC` などを手で設定しない）。
- 実行ファイルは `qt_add_executable()`。`add_executable()` を直接使わない（デプロイ用の情報が付かない）。
- `UTSUSHI_WERROR=ON` のとき警告をエラー扱いにする。CI では常に ON。

---

## 受け入れ基準（この規約の遵守確認）

- [ ] `grep -rn "QRegExp\|SIGNAL(\|SLOT(\|foreach\|qrand\|QString::null" --include=*.cpp --include=*.hpp .` が 0 件
- [ ] `grep -rn "throw \|try {\|catch (" --include=*.cpp --include=*.hpp .` が 0 件
- [ ] `grep -rn "processEvents" --include=*.cpp .` が 0 件
- [ ] `core/` 配下に `QtWidgets` の include が 0 件
- [ ] `-Wall -Wextra -Werror` でビルドが通る
