#include "core/converter.hpp"

#include "core/output_path.hpp"
#include "core/render_size.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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

void Converter::run(const std::vector<ConversionJob>& jobs) {
    m_cancelRequested.store(false, std::memory_order_relaxed);
    ConversionSummary summary;
    const int fileCount = static_cast<int>(jobs.size());
    int fileIndex = 0;

    for (const ConversionJob& job : jobs) {
        ++fileIndex;
        emit fileStarted(fileIndex, fileCount, job.inputPdfPath);

        // 出力先の書き込み可否は変換開始前に検査。書けなければバッチ全体を中止。
        const QDir outDir(job.outputDirPath);
        if (!outDir.exists() || !QFileInfo(outDir.absolutePath()).isWritable()) {
            summary.aborted = true;
            summary.failures.push_back({job.inputPdfPath, 0,
                QCoreApplication::translate("Converter", "出力先に書き込めません: %1")
                    .arg(job.outputDirPath)});
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
        const QString stem = QFileInfo(job.inputPdfPath).completeBaseName();
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
                continue;
            }
            const auto size = renderSizeFor(doc->pagePointSize(pageIndex), job.dpi);
            if (!size) {
                summary.failures.push_back({job.inputPdfPath, pageNumber,
                    QCoreApplication::translate("Converter", "DPI が大きすぎます")});
                ++summary.failedPages;
                continue;
            }
            const QImage image = doc->render(pageIndex, *size);
            if (image.isNull()) {
                summary.failures.push_back({job.inputPdfPath, pageNumber,
                    QCoreApplication::translate("Converter", "ページのレンダリングに失敗しました")});
                ++summary.failedPages;
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
                // 書き込み失敗 = 出力先の異常。バッチ全体を中止する
                summary.aborted = true;
                summary.failures.push_back({job.inputPdfPath, pageNumber,
                    QCoreApplication::translate("Converter", "PNG の保存に失敗しました: %1")
                        .arg(outPath)});
                ++summary.failedPages;
                emit finished(summary);
                return;
            }
            ++summary.succeededPages;
            ++doneInFile;
            emit pageDone(doneInFile, plannedInFile);
        }
    }
    emit finished(summary);
}

} // namespace utsushi
