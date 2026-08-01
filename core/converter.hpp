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

    // UI スレッドから呼ぶ。ワーカーはファイル境界・ページ境界でフラグを確認して停止する。
    void requestCancel();

    // 新しいバッチを run() に渡す直前に UI スレッドから呼ぶ。
    // run() 自身はこのフラグをリセットしない：run() が実際にワーカースレッドで
    // 実行され始めるまでの間（UI がキューイング接続で run() を呼んでから
    // ワーカーがそれを処理するまでの window）に requestCancel() が届いても
    // 取りこぼさないため。そのため「前回バッチのキャンセル状態を次のバッチへ
    // 持ち越さない」責務は呼び出し側がこのメソッドで明示的に果たす。
    // std::atomic なのでスレッドをまたいで呼んでも安全。
    void resetCancel();

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
