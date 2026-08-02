#pragma once
#include "core/conversion_job.hpp"

#include <QFile>
#include <QImage>
#include <QObject>
#include <QPdfDocument>
#include <atomic>

namespace utsushi {

// QPdfDocument::Error を利用者向けの文言へ変換する。UI の事前検査（MainWindow::buildJobs）
// とワーカー（Converter::run）の両方が同じ変換ロジックを再利用することで、
// 同じ失敗原因（例: パスワード保護・暗号化された PDF）に対して片方だけ具体的な理由を示し、
// もう片方は一律の汎用文言に潰す、という不整合を防ぐ
// （2026-08-02 レビュー Blocker 修正）。
// core は QtWidgets を使わないが、QObject 由来の翻訳機構である
// QCoreApplication::translate は core からも使ってよい（tr() は使わない）。
[[nodiscard]] QString loadErrorText(QPdfDocument::Error error);

// file（呼び出し前に書き込み用に開かれていること）へ image を PNG として書き込む。
// 失敗した場合、file を閉じて削除する。
//
// 呼び出し規約: file は必ず QIODevice::NewOnly で「呼び出し側がこの呼び出しの直前に
// 新規作成した」ものであること。この関数はその前提の上で削除するため、他プロセスや
// 以前から存在した既存ファイルを誤って削除することはない。
//
// Skip/Rename の保存先確保を「存在確認してから書く」のではなく「排他的に確保してから
// 書く」方式に変えるための部品（2026-08-02 レビュー Blocker 修正）。匿名名前空間に
// 閉じず公開しているのは、Converter::run() の通常経路では image.isNull() が
// この手前で既に弾かれるため、null 画像による保存失敗という経路そのものへ
// 到達できず、この関数を直接呼ばない限り失敗時のクリーンアップを検証できないため。
[[nodiscard]] bool writeImageExclusive(QFile& file, const QImage& image);

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
