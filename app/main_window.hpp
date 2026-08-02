#pragma once
#include "core/conversion_job.hpp"

#include <QMainWindow>
#include <QThread>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace utsushi {

class Converter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

signals:
    // queued 接続でワーカースレッドの Converter::run へ渡す
    void conversionRequested(const std::vector<utsushi::ConversionJob>& jobs);

private:
    void setupUi();
    void setupWorker();
    void addFiles();
    void removeSelectedFiles();
    void chooseOutputDir();
    void startConversion();
    // ジョブ構築。UI スレッド専用の QPdfDocument で各ファイルを事前検査し、
    // ページ数取得・範囲検証・パスワード取得（1 回だけ問う）を行う。
    // 検証に失敗したファイルは jobs に入れず failures に積む。
    [[nodiscard]] std::vector<ConversionJob> buildJobs(std::vector<PageFailure>& failures);
    void onFileStarted(int fileIndex, int fileCount, const QString& filePath);
    void onPageDone(int done, int total);
    void onFinished(const ConversionSummary& summary);
    void setRunning(bool running);
    void showSummary(const ConversionSummary& summary,
                     const std::vector<PageFailure>& upfrontFailures);

    QListWidget* m_fileList = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QLineEdit* m_pageRangeEdit = nullptr;
    QComboBox* m_dpiCombo = nullptr;
    QLineEdit* m_outputDirEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QComboBox* m_overwriteCombo = nullptr;
    QPushButton* m_convertButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_progressLabel = nullptr;       // テキスト進捗「120 ページ中 37 ページ完了」
    QPlainTextEdit* m_summaryView = nullptr; // 変換結果サマリ（読み取り専用）

    QThread m_workerThread;
    Converter* m_converter = nullptr;        // m_workerThread 所有。親は持たせない
    int m_currentFileIndex = 0;
    int m_fileCount = 0;
    // buildJobs() が事前検査で弾いた失敗（ジョブ化されずワーカーに渡らない）。
    // startConversion() の先頭でクリアし、run() が finished を emit するまで
    // 保持する。onFinished() の showSummary() 呼び出しに渡すことで、
    // 最終サマリからこれらの失敗が消えないようにする（Fix round 1, finding 1）。
    std::vector<PageFailure> m_upfrontFailures;
};

} // namespace utsushi
