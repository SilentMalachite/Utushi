#pragma once
#include <QMetaType>
#include <QString>
#include <vector>

namespace utsushi {

// 既存出力ファイルがあるときの挙動。既定は Skip（黙って上書きしない）。
enum class OverwritePolicy { Skip, Overwrite, Rename };

// 1 つの PDF ファイルに対する変換指示。不変の値オブジェクト。
// スレッド間はコピーで渡す（ポインタを渡さない）。
struct ConversionJob {
    QString inputPdfPath;
    QString outputDirPath;
    std::vector<int> pages;   // 1 始まり。空なら全ページ
    double dpi = 300.0;
    OverwritePolicy overwritePolicy = OverwritePolicy::Skip;
    QString password;         // パスワード保護 PDF 用。UI 側が取得して渡す（空なら無し）
};

struct PageFailure {
    QString filePath;
    int pageNumber = 0;       // 1 始まり。ファイル自体が開けない失敗は 0
    QString reason;           // UI にそのまま表示する QString（tr 済みの文言を入れる）
};

struct ConversionSummary {
    int succeededPages = 0;
    int failedPages = 0;
    int skippedPages = 0;     // OverwritePolicy::Skip により既存のためスキップした数
    bool cancelled = false;   // ユーザーキャンセルで途中終了した
    bool aborted = false;     // 出力先に書き込めない等でバッチ全体を中止した
    std::vector<PageFailure> failures;
};

} // namespace utsushi

Q_DECLARE_METATYPE(utsushi::ConversionJob)
Q_DECLARE_METATYPE(utsushi::ConversionSummary)
Q_DECLARE_METATYPE(std::vector<utsushi::ConversionJob>)
