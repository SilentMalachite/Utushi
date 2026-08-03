# Utsushi（写し）

PDF を PNG 連番画像に変換する、完全オフラインのデスクトップアプリ。

[![CI](https://github.com/SilentMalachite/Utushi/actions/workflows/ci.yml/badge.svg)](https://github.com/SilentMalachite/Utushi/actions/workflows/ci.yml)

> **綴りについて**: リポジトリ名は `Utushi`、アプリ名は `Utsushi`（写し）です。1 文字違いますが同じものを指します。

---

## 特徴

- **完全オフライン** — ネットワーク通信を一切行いません。テレメトリ・自動更新・クラッシュレポータを持ちません。
- **外部プロセスに依存しない** — ラスタライズは Qt 標準の QtPdf（`QPdfDocument`）のみ。Poppler / MuPDF / Ghostscript の同梱もインストールも不要です。
- **バッチ変換** — 複数の PDF をまとめて変換します。1 件失敗しても残りは処理を続け、最後に理由付きのサマリを表示します。
- **既存ファイルを黙って上書きしない** — 既定は「スキップ」。上書き・別名保存はユーザーが明示的に選びます。
- **アクセシビリティ** — アニメーションを使わず状態遷移は即時。全操作がキーボードだけで完結し、進捗はプログレスバーとテキストの両方で示します。変換はワーカースレッドで行うため、長時間の変換中も UI が固まりません。

## 対応環境

| 項目 | 内容 |
| --- | --- |
| macOS | arm64（Apple Silicon） |
| Windows | x64 |
| Qt | 6.8 以上（開発機は 6.11.1、CI は macOS 6.11.x / Windows 6.10.x） |
| ビルド | CMake 3.21 以上、C++20 対応コンパイラ |

Linux でも動くように書いていますが、CI では検証していません。

## 状態

**v0.1.0 をリリースしました**（2026-08-04）。macOS (arm64) と Windows (x64) のビルドを [Releases](https://github.com/SilentMalachite/Utushi/releases/latest) から入手できます。ソースからビルドすることもできます（[docs/build.md](docs/build.md)）。

**配布物は未署名です。** 初回起動時に macOS では Gatekeeper、Windows では SmartScreen に警告されます。開き方は [docs/known-issues.md](docs/known-issues.md#配布物に署名や公証をしていない) を参照してください。

- 受け入れ基準の達成状況: [CLAUDE.md の「受け入れ基準（v0.1.0）」](CLAUDE.md#受け入れ基準v010)
- 既知の問題と制限: [docs/known-issues.md](docs/known-issues.md)

## 使い方（要約）

1. **ファイルを追加** で PDF を選ぶ（複数選択可）
2. **ページ範囲**（空欄で全ページ。`1-5,8,11-` 形式）、**解像度 DPI**、**出力先**、**既存ファイル**の扱いを指定する
3. **変換開始**

出力されるファイル名は `{PDF のファイル名}_p{ゼロ埋めページ番号}.png` です。
例: `report.pdf` の 7 ページ目（全 120 ページ）→ `report_p007.png`

詳細は [docs/usage.md](docs/usage.md) を参照してください。

## スコープ外

以下は意図的に実装していません。

- PDF の編集・結合・分割
- OCR、テキスト抽出
- PNG 以外の出力形式（JPEG / TIFF / WebP は v0.2 以降で検討）
- パスワード保護・暗号化された PDF を開くこと（理由は [docs/known-issues.md](docs/known-issues.md#パスワード保護や暗号化された-pdf-は開けない)）
- クラウド連携、自動更新、テレメトリ
- 変換の並列化（v0.1 ではワーカースレッド 1 本。v0.2 以降で検討）

## ドキュメント

| ファイル | 内容 | 主な読者 |
| --- | --- | --- |
| [docs/usage.md](docs/usage.md) | 使い方と仕様の詳細、エラーメッセージ一覧 | 使う人 |
| [docs/build.md](docs/build.md) | ビルド・テスト・静的解析・配布物の作り方 | 開発する人 |
| [docs/known-issues.md](docs/known-issues.md) | 既知の問題と制限 | 全員 |
| [CHANGELOG.md](CHANGELOG.md) | 変更履歴 | 全員 |
| [docs/qt-conventions.md](docs/qt-conventions.md) | C++20 / Qt6 の実装規約 | 実装する人 |
| [CLAUDE.md](CLAUDE.md) | 設計方針と受け入れ基準（AI 協働のための恒久指示） | 実装する人 |
| [AGENTS.md](AGENTS.md) | レビュー担当への契約（不変条件と検証手順） | レビューする人 |
| [docs/progress.md](docs/progress.md) | 開発の進捗ログと決定事項 | 開発する人 |

## ライセンス

本体は **GNU General Public License v3.0（GPLv3）** で配布します。全文は [LICENSE](LICENSE) を参照してください。

- **Qt 6** — LGPLv3。**動的リンク**で使用しています（静的リンクや Qt ソースの改変は行っていません）。
  Qt のソースコード: <https://download.qt.io/official_releases/qt/>
  LGPLv3 全文: <https://www.gnu.org/licenses/lgpl-3.0.html>
- **PDFium**（QtPdf が内部で使用）— BSD-3-Clause

アプリの `ヘルプ > ライセンス` からも同じ告知を表示できます。配布物には `LICENSE` が同梱されます（macOS は `utsushi.app/Contents/Resources/LICENSE`、Windows は実行ファイルと同じディレクトリ）。
