#include "core/converter.hpp"

#include "core/output_path.hpp"
#include "core/render_size.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPdfDocument>

#include <memory>

namespace utsushi {
namespace {

// エラー種別ごとの表示文言。core は Widgets を使わないが tr() は QObject のもので可。
QString loadErrorText(QPdfDocument::Error error) {
    switch (error) {
    case QPdfDocument::Error::IncorrectPassword:
        return QCoreApplication::translate("Converter", "パスワードが違うか、未入力です");
    case QPdfDocument::Error::FileNotFound:
        return QCoreApplication::translate("Converter", "ファイルが見つかりません");
    case QPdfDocument::Error::InvalidFileFormat:
    case QPdfDocument::Error::DataNotYetAvailable:
    case QPdfDocument::Error::UnsupportedSecurityScheme:
    default:
        return QCoreApplication::translate("Converter", "PDF として読み込めません");
    }
}

// Rename ポリシー: report_p001.png → report_p001_2.png, _3, ... の空き番号を返す。
QString renamedPath(const QDir& dir, const QString& fileName) {
    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString ext = info.suffix();
    for (int n = 2;; ++n) {
        const QString candidate = dir.filePath(base + u'_' + QString::number(n) + u'.' + ext);
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

} // namespace

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
        if (!job.password.isEmpty()) {
            doc->setPassword(job.password);
        }
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
            const QImage image = doc->render(pageIndex, *size);
            if (image.isNull()) {
                summary.failures.push_back({job.inputPdfPath, pageNumber,
                    QCoreApplication::translate("Converter", "ページのレンダリングに失敗しました")});
                ++summary.failedPages;
                ++doneInFile;
                emit pageDone(doneInFile, plannedInFile);
                continue;
            }

            QString outPath = outDir.filePath(outputFileName(stem, pageNumber, totalPages));
            if (QFileInfo::exists(outPath)) {
                if (job.overwritePolicy == OverwritePolicy::Skip) {
                    ++summary.skippedPages;
                    ++doneInFile;
                    emit pageDone(doneInFile, plannedInFile);
                    continue;
                }
                if (job.overwritePolicy == OverwritePolicy::Rename) {
                    outPath = renamedPath(outDir, QFileInfo(outPath).fileName());
                }
                // Overwrite はそのまま保存
            }
            if (!image.save(outPath, "PNG")) {
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
