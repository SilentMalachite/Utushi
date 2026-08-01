#pragma once
#include "core/conversion_job.hpp"

#include <QObject>
#include <atomic>

namespace utsushi {

// ワーカースレッドへ moveToThread して使う（QThread は継承しない）。
// QPdfDocument は run() の中でのみ生成・使用する = ワーカースレッド局所。
// UI スレッドの QPdfDocument と共有しない。
class Converter : public QObject {
    Q_OBJECT
public:
    explicit Converter(QObject* parent = nullptr);

    // UI スレッドから呼ぶ。ワーカーはページ境界でフラグを確認して停止する。
    void requestCancel();

public slots:
    void run(const std::vector<utsushi::ConversionJob>& jobs);

signals:
    void fileStarted(int fileIndex1Based, int fileCount, const QString& filePath);
    void pageDone(int done, int total);   // 現在のファイル内での完了数 / 予定数
    void finished(const utsushi::ConversionSummary& summary);

private:
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace utsushi
