#include "core/converter.hpp"

#include "core/output_path.hpp"
#include "core/render_size.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>

#include <memory>

namespace utsushi {

// エラー種別ごとの表示文言。core は Widgets を使わないが QCoreApplication::translate は
// QObject 由来の翻訳機構なので core から使ってよい（tr() は使わない）。
// app/main_window.cpp の buildJobs() 事前検査からも再利用するため、匿名名前空間に
// 閉じず外部リンケージにして converter.hpp で宣言している
// （2026-08-02 レビュー Blocker 修正: 事前検査と worker とで表示が食い違わないように）。
QString loadErrorText(QPdfDocument::Error error) {
    switch (error) {
    case QPdfDocument::Error::IncorrectPassword:
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        // パスワード保護・暗号化 PDF は非対応スコープ（開発者判断）。QPdfWriter が
        // 暗号化 PDF を作れず、この経路を検証するフィクスチャが作れないため、
        // パスワード再入力機能そのものを持たない。「パスワードが違います」のような
        // 再試行を示唆する文言や、汎用の「読み込めません」（壊れているという印象を
        // 与える）は使わず、非対応であることを明示する。
        return QCoreApplication::translate("Converter",
            "パスワード保護・暗号化された PDF には対応していません");
    case QPdfDocument::Error::FileNotFound:
        return QCoreApplication::translate("Converter", "ファイルが見つかりません");
    case QPdfDocument::Error::InvalidFileFormat:
    case QPdfDocument::Error::DataNotYetAvailable:
    default:
        return QCoreApplication::translate("Converter", "PDF として読み込めません");
    }
}

namespace {

// Rename ポリシー: report_p001.png → report_p001_2.png, _3, ... の空き番号を確保する。
// 「exists() で候補を選んでから書く」のではなく、各候補を QIODevice::NewOnly で
// 実際に排他オープンできるかどうかで確保する。存在確認と書き込みの間に別プロセスが
// 同じ候補を取ってしまう競合窓をなくすため（2026-08-02 レビュー Blocker 修正）。
// 成功したら、確保済み（オープン済み）の QFile をそのまま返す。
std::unique_ptr<QFile> claimRenamedFile(const QDir& dir, const QString& fileName) {
    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString ext = info.suffix();
    // 書き込み不能な状況が続いた場合に無限ループへ陥らないための防御的な上限。
    // 通常の使用でこの上限に達することはない。
    constexpr int kMaxRenameAttempts = 10000;
    for (int n = 2; n <= kMaxRenameAttempts; ++n) {
        auto file = std::make_unique<QFile>(
            dir.filePath(base + u'_' + QString::number(n) + u'.' + ext));
        if (file->open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            return file;
        }
    }
    return nullptr;
}

} // namespace

bool writeImageExclusive(QFile& file, const QImage& image) {
    if (image.save(&file, "PNG")) {
        return true;
    }
    // 呼び出し規約により、ここに来る file は「呼び出し側がこの呼び出しの直前に
    // QIODevice::NewOnly で新規作成した」ものだけ。したがって削除しても、
    // 他プロセスや以前から存在した既存ファイルを消すことにはならない。
    // 確保（open）の時点で 0 バイトのファイルが既にディスク上にできているため、
    // ここで消さないと保存失敗のたびに空の PNG が残る。
    file.close();
    file.remove();
    return false;
}

Converter::Converter(QObject* parent) : QObject(parent) {}

void Converter::requestCancel() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

void Converter::resetCancel() {
    m_cancelRequested.store(false, std::memory_order_relaxed);
}

void Converter::run(const std::vector<ConversionJob>& jobs) {
    // 注意: ここで m_cancelRequested をリセットしない。run() が呼ばれる前に
    // 届いた requestCancel() を取りこぼさないため（Fix round 1, finding 2）。
    // 前回バッチの状態を持ち越さない責務は呼び出し側の resetCancel() が担う。
    ConversionSummary summary;
    const int fileCount = static_cast<int>(jobs.size());
    int fileIndex = 0;

    // 出力先ディレクトリ + stem が重複するジョブを検出する。同じバッチ内で
    // 別ディレクトリの同名ファイル（例: ~/x/report.pdf と ~/y/report.pdf）を
    // 同じ出力先へ変換すると、どちらも "report_pNNN.png" という同じファイル名を
    // 生成する。Skip では 2 件目が理由不明のまま黙って消え、Overwrite では
    // 2 件目が 1 件目の出力を同一バッチ内で破壊する（ユーザーが Overwrite で
    // 意図したのは「前回実行の古い出力を上書きする」ことであり、これではない）。
    // バッチ内で最初に現れたファイルだけを処理し、以降の同名衝突は
    // レンダリングを試みず、既存の失敗報告チャンネルで衝突として報告する
    // （Fix wave 2, finding 1）。
    QHash<QString, QString> claimedOutputs;   // key: 出力先+stem → 先に確保した入力ファイル

    for (const ConversionJob& job : jobs) {
        // ファイル境界でもキャンセルを確認する。run() 開始前に届いたキャンセルも
        // ここで最初に検出される（1 ファイル目にも着手しない）。
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            summary.cancelled = true;
            emit finished(summary);
            return;
        }
        ++fileIndex;
        emit fileStarted(fileIndex, fileCount, job.inputPdfPath);

        const QString stem = QFileInfo(job.inputPdfPath).completeBaseName();
        // 衝突判定は実際に書き出されるファイル名の元になる値で行う。outputFileName()
        // は内部で sanitizedStem() を通すため、生の completeBaseName() だけを比較すると
        // 別々の生 stem が同じ正規化結果へ丸められるケースを見逃す
        // （例: "a:b" と "a?b" はどちらも "a_b" に正規化され、同じ
        // "a_b_p001.png" を生成する。Fix wave 2, re-review finding）。
        // macOS (APFS 既定) と Windows (NTFS 既定) はいずれもファイルシステムが既定で
        // 大文字小文字を区別しないため、比較キーも toCaseFolded() で畳み込む
        // （"Report.pdf" と "report.pdf" は同じファイルに書き込まれる）。
        const QString outputKey =
            (QDir(job.outputDirPath).absolutePath() + QChar(u'\x1f') + sanitizedStem(stem))
                .toCaseFolded();
        const auto claimedIt = claimedOutputs.constFind(outputKey);
        if (claimedIt != claimedOutputs.constEnd()) {
            // 同じファイルが一覧に重複して追加されているだけの場合は、「自分自身と
            // 重複」という不自然な文言にしない（実害はなく最初の 1 回だけ変換される）。
            const QString reason = (claimedIt.value() == job.inputPdfPath)
                ? QCoreApplication::translate("Converter",
                      "同じファイルが変換対象に複数回追加されています"
                      "（最初の 1 回だけ変換します）")
                : QCoreApplication::translate("Converter",
                      "出力ファイル名が %1 と重複するため変換していません"
                      "（同じ出力先に同じファイル名で書き込まれます）")
                      .arg(claimedIt.value());
            summary.failures.push_back({job.inputPdfPath, 0, reason});
            ++summary.failedPages;
            continue;   // 1 件の衝突でバッチを止めない。このファイルだけ変換しない
        }
        claimedOutputs.insert(outputKey, job.inputPdfPath);

        // 出力先の書き込み可否は変換開始前に検査。書けなければバッチ全体を中止。
        const QDir outDir(job.outputDirPath);
        if (!outDir.exists() || !QFileInfo(outDir.absolutePath()).isWritable()) {
            summary.aborted = true;
            summary.failures.push_back({job.inputPdfPath, 0,
                QCoreApplication::translate("Converter", "出力先に書き込めません: %1")
                    .arg(job.outputDirPath)});
            ++summary.failedPages;
            break;
        }

        // QPdfDocument はワーカースレッド（= このメソッドの実行スレッド）だけが触る
        auto doc = std::make_unique<QPdfDocument>();
        if (doc->load(job.inputPdfPath) != QPdfDocument::Error::None) {
            summary.failures.push_back({job.inputPdfPath, 0, loadErrorText(doc->error())});
            ++summary.failedPages;
            continue;   // 1 件の失敗でバッチを止めない
        }

        const int totalPages = doc->pageCount();
        std::vector<int> pages = job.pages;
        if (pages.empty()) {
            for (int p = 1; p <= totalPages; ++p) {
                pages.push_back(p);
            }
        }
        // stem はファイル冒頭（衝突検出時）で計算済み
        const int plannedInFile = static_cast<int>(pages.size());
        int doneInFile = 0;

        for (const int pageNumber : pages) {   // pageNumber は 1 始まり
            if (m_cancelRequested.load(std::memory_order_relaxed)) {
                summary.cancelled = true;
                emit finished(summary);
                return;   // 途中までの PNG はそのまま残す
            }
            // 1 始まり → 0 始まりの変換はこの 1 行のみ（不変条件 #10）
            const int pageIndex = pageNumber - 1;
            if (pageNumber < 1 || pageNumber > totalPages) {
                summary.failures.push_back({job.inputPdfPath, pageNumber,
                    QCoreApplication::translate("Converter", "ページ番号が範囲外です")});
                ++summary.failedPages;
                // 失敗したページも「試みた」うちに数える。ここで doneInFile を進めないと
                // 最後のページが失敗で終わったとき progress が plannedInFile に到達せず、
                // プログレスバーが 100% にならないまま「完了」表示だけが出る矛盾が起きる
                // （Fix wave 2, finding 3）。
                ++doneInFile;
                emit pageDone(doneInFile, plannedInFile);
                continue;
            }
            const auto size = renderSizeFor(doc->pagePointSize(pageIndex), job.dpi);
            if (!size) {
                summary.failures.push_back({job.inputPdfPath, pageNumber,
                    QCoreApplication::translate("Converter", "DPI が大きすぎます")});
                ++summary.failedPages;
                ++doneInFile;
                emit pageDone(doneInFile, plannedInFile);
                continue;
            }
            QImage image = doc->render(pageIndex, *size);
            if (image.isNull()) {
                summary.failures.push_back({job.inputPdfPath, pageNumber,
                    QCoreApplication::translate("Converter", "ページのレンダリングに失敗しました")});
                ++summary.failedPages;
                ++doneInFile;
                emit pageDone(doneInFile, plannedInFile);
                continue;
            }
            // QPdfDocument::render() は紙の色を塗らず、背景が完全に透明なアルファ付き
            // 画像を返す（実測: A4 300dpi の 1 ページで 8,696,332px 中 8,618,300px が
            // alpha=0、不透明な白は 0px）。そのまま PNG にすると「白い紙に黒い文字」
            // ではなく「透明な背景に黒い文字」になり、ダークモードのビューアでは背景が
            // 暗く表示されて文字が溶け、何も見えなくなる（開発者報告、2026-08-04）。
            // 描画済みの内容の「下」へ白を敷いて紙を再現する。
            // CompositionMode_DestinationOver で既存の画素を上書きせずに合成するため、
            // 白い画像をもう 1 枚確保して重ねる方式と違い、ピークメモリを倍にしない
            //（1 辺 20000px の上限では 1 枚で 1.6GB に達する）。
            QPainter painter(&image);
            painter.setCompositionMode(QPainter::CompositionMode_DestinationOver);
            painter.fillRect(image.rect(), Qt::white);
            painter.end();

            QString outPath = outDir.filePath(outputFileName(stem, pageNumber, totalPages));
            bool saved = false;
            if (job.overwritePolicy == OverwritePolicy::Overwrite) {
                // Overwrite だけはユーザーが明示的に上書きを許可した唯一のポリシー。
                // 既存ファイルを置き換えてよいので、そのまま保存する。
                saved = image.save(outPath, "PNG");
            } else {
                // Skip/Rename は「exists() で確認してから書く」のではなく、
                // QIODevice::NewOnly で保存先そのものを排他的に確保してから書く。
                // 確認と書き込みの間には別プロセス（別の Utsushi インスタンス等）が
                // 同名ファイルを作れる窓があり、確認後に書くだけでは、その後発の
                // 書き込みが明示的な Overwrite なしに中身を破壊し得るため
                // （2026-08-02 レビュー Blocker 修正）。
                QFile exclusiveOutput(outPath);
                if (exclusiveOutput.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                    saved = writeImageExclusive(exclusiveOutput, image);
                } else if (job.overwritePolicy == OverwritePolicy::Skip) {
                    // 確保できなかった = 保存先は既に何かで埋まっている。書かずに次へ進む。
                    ++summary.skippedPages;
                    ++doneInFile;
                    emit pageDone(doneInFile, plannedInFile);
                    continue;
                } else {
                    // Rename: 空いている "_N" 候補を、確認してから書くのではなく
                    // 1 つずつ実際に排他オープンを試みることで確保する。
                    auto renamedFile = claimRenamedFile(outDir, QFileInfo(outPath).fileName());
                    if (!renamedFile) {
                        summary.failures.push_back({job.inputPdfPath, pageNumber,
                            QCoreApplication::translate("Converter",
                                "別名保存先の候補を確保できませんでした: %1").arg(outPath)});
                        ++summary.failedPages;
                        ++doneInFile;
                        emit pageDone(doneInFile, plannedInFile);
                        continue;
                    }
                    outPath = renamedFile->fileName();
                    saved = writeImageExclusive(*renamedFile, image);
                }
            }
            if (!saved) {
                // 書き込み失敗はこのページ・このファイルだけの問題として扱う。
                // 出力先ディレクトリ自体の存在・書き込み可否は、このループへ入る前に
                // 既に検査済み（上の outDir チェック）。ここでの失敗を無条件に
                // バッチ全体の中止へ格上げすると、同じバッチ内の後続の正常な
                // ファイルまで変換されなくなり、「1 件の失敗で全体を止めない」
                // 契約（AGENTS.md #6 / CLAUDE.md）に反する
                // （2026-08-02 レビュー Blocker 修正）。
                summary.failures.push_back({job.inputPdfPath, pageNumber,
                    QCoreApplication::translate("Converter", "PNG の保存に失敗しました: %1")
                        .arg(outPath)});
                ++summary.failedPages;
                ++doneInFile;
                emit pageDone(doneInFile, plannedInFile);
                continue;
            }
            ++summary.succeededPages;
            ++doneInFile;
            emit pageDone(doneInFile, plannedInFile);
        }
    }
    emit finished(summary);
}

} // namespace utsushi
