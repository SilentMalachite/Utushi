#include "main_window.hpp"

#include "core/converter.hpp"
#include "core/page_range.hpp"
#include "core/render_size.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPdfDocument>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

#include <memory>

namespace utsushi {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setupUi();
    setupWorker();
}

MainWindow::~MainWindow() {
    // quit() は「ワーカーのイベントループがアイドルになったら止まる」予約でしか
    // ない。Converter::run() はバッチが終わる・キャンセルされる・中止されるまで
    // イベントループへ戻らない同期処理なので、requestCancel() を先に呼ばないと
    // wait() が残りバッチ全体の完了までブロックし、ウィンドウを閉じる／アプリ
    // 終了操作が長時間フリーズして見える（Fix round 1, finding 2。150 ページの
    // ジョブで実測 requestCancel() なし 36.5 秒 → あり 233ms）。
    // requestCancel() は std::atomic 経由でスレッドをまたいで安全。
    m_converter->requestCancel();
    m_workerThread.quit();
    m_workerThread.wait();
}

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    // --- 入力ファイル ---
    m_fileList = new QListWidget(central);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setAccessibleName(tr("変換する PDF ファイルの一覧"));
    m_addButton = new QPushButton(tr("ファイルを追加(&A)..."), central);
    m_removeButton = new QPushButton(tr("選択を削除(&R)"), central);
    auto* fileButtons = new QHBoxLayout;
    fileButtons->addWidget(m_addButton);
    fileButtons->addWidget(m_removeButton);
    fileButtons->addStretch();
    rootLayout->addWidget(new QLabel(tr("入力 PDF:"), central));
    rootLayout->addWidget(m_fileList);
    rootLayout->addLayout(fileButtons);

    // --- 変換設定 ---
    auto* form = new QFormLayout;
    m_pageRangeEdit = new QLineEdit(central);
    m_pageRangeEdit->setPlaceholderText(tr("空欄で全ページ。例: 1-5,8,11-"));
    form->addRow(tr("ページ範囲(&P):"), m_pageRangeEdit);

    m_dpiCombo = new QComboBox(central);
    m_dpiCombo->setEditable(true);   // 任意値の入力を許す
    for (const int dpi : kStandardDpiPresets) {
        m_dpiCombo->addItem(QString::number(dpi));
    }
    m_dpiCombo->setCurrentText(QStringLiteral("300"));
    form->addRow(tr("解像度 DPI(&D):"), m_dpiCombo);

    m_outputDirEdit = new QLineEdit(central);
    m_browseButton = new QPushButton(tr("参照(&B)..."), central);
    auto* outRow = new QHBoxLayout;
    outRow->addWidget(m_outputDirEdit);
    outRow->addWidget(m_browseButton);
    form->addRow(tr("出力先(&O):"), outRow);

    m_overwriteCombo = new QComboBox(central);
    m_overwriteCombo->addItem(tr("スキップ（既定・上書きしない）"),
                              QVariant::fromValue(static_cast<int>(OverwritePolicy::Skip)));
    m_overwriteCombo->addItem(tr("上書きする"),
                              QVariant::fromValue(static_cast<int>(OverwritePolicy::Overwrite)));
    m_overwriteCombo->addItem(tr("別名で保存（_2 を付与）"),
                              QVariant::fromValue(static_cast<int>(OverwritePolicy::Rename)));
    form->addRow(tr("既存ファイル(&E):"), m_overwriteCombo);
    rootLayout->addLayout(form);

    // --- 実行・進捗 ---
    m_convertButton = new QPushButton(tr("変換開始(&S)"), central);
    m_convertButton->setDefault(true);
    m_cancelButton = new QPushButton(tr("キャンセル(&C)"), central);
    m_cancelButton->setEnabled(false);
    auto* runRow = new QHBoxLayout;
    runRow->addWidget(m_convertButton);
    runRow->addWidget(m_cancelButton);
    runRow->addStretch();
    rootLayout->addLayout(runRow);

    m_progressBar = new QProgressBar(central);
    m_progressBar->setTextVisible(false);   // テキストは m_progressLabel で出す
    m_progressLabel = new QLabel(tr("待機中"), central);
    rootLayout->addWidget(m_progressBar);
    rootLayout->addWidget(m_progressLabel);

    m_summaryView = new QPlainTextEdit(central);
    m_summaryView->setReadOnly(true);
    m_summaryView->setAccessibleName(tr("変換結果サマリ"));
    rootLayout->addWidget(new QLabel(tr("結果:"), central));
    rootLayout->addWidget(m_summaryView);

    setCentralWidget(central);
    setWindowTitle(QStringLiteral("Utsushi"));

    // タブ順序を明示（キーボードだけで全操作が完結すること）
    QWidget::setTabOrder(m_fileList, m_addButton);
    QWidget::setTabOrder(m_addButton, m_removeButton);
    QWidget::setTabOrder(m_removeButton, m_pageRangeEdit);
    QWidget::setTabOrder(m_pageRangeEdit, m_dpiCombo);
    QWidget::setTabOrder(m_dpiCombo, m_outputDirEdit);
    QWidget::setTabOrder(m_outputDirEdit, m_browseButton);
    QWidget::setTabOrder(m_browseButton, m_overwriteCombo);
    QWidget::setTabOrder(m_overwriteCombo, m_convertButton);
    QWidget::setTabOrder(m_convertButton, m_cancelButton);
    QWidget::setTabOrder(m_cancelButton, m_summaryView);

    connect(m_addButton, &QPushButton::clicked, this, &MainWindow::addFiles);
    connect(m_removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedFiles);
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::chooseOutputDir);
    connect(m_convertButton, &QPushButton::clicked, this, &MainWindow::startConversion);
}

void MainWindow::setupWorker() {
    // Converter は親を持たせず moveToThread（親付き QObject は move できない）
    m_converter = new Converter;
    m_converter->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_converter, &QObject::deleteLater);

    // UI → ワーカー（queued。値オブジェクトのコピーで渡る）
    connect(this, &MainWindow::conversionRequested, m_converter, &Converter::run);
    // ワーカー → UI（queued。UI 更新は必ずこちらのスレッドで行われる）
    connect(m_converter, &Converter::fileStarted, this, &MainWindow::onFileStarted);
    connect(m_converter, &Converter::pageDone, this, &MainWindow::onPageDone);
    connect(m_converter, &Converter::finished, this, &MainWindow::onFinished);
    // キャンセルは atomic フラグを直接叩く（queued にしない。即時性が必要）
    connect(m_cancelButton, &QPushButton::clicked, m_converter,
            [converter = m_converter] { converter->requestCancel(); },
            Qt::DirectConnection);

    m_workerThread.start();
}

void MainWindow::addFiles() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("PDF を選択"), QString(), tr("PDF ファイル (*.pdf)"));
    for (const QString& path : paths) {
        m_fileList->addItem(QDir::toNativeSeparators(path));
    }
}

void MainWindow::removeSelectedFiles() {
    const auto items = m_fileList->selectedItems();
    for (QListWidgetItem* item : items) {
        delete item;   // QListWidget から所有権を引き取って削除
    }
}

void MainWindow::chooseOutputDir() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("出力先を選択"));
    if (!dir.isEmpty()) {
        m_outputDirEdit->setText(QDir::toNativeSeparators(dir));
    }
}

std::vector<ConversionJob> MainWindow::buildJobs(std::vector<PageFailure>& failures) {
    std::vector<ConversionJob> jobs;
    bool okDpi = false;
    const double dpi = m_dpiCombo->currentText().toDouble(&okDpi);
    if (!okDpi || dpi <= 0.0) {
        failures.push_back({QString(), 0, tr("DPI の値が不正です: %1")
                                              .arg(m_dpiCombo->currentText())});
        return jobs;
    }
    const auto policy =
        static_cast<OverwritePolicy>(m_overwriteCombo->currentData().toInt());
    const QString outDir = m_outputDirEdit->text();

    for (int i = 0; i < m_fileList->count(); ++i) {
        const QString path = m_fileList->item(i)->text();
        // UI スレッド専用の QPdfDocument で事前検査（ワーカーの doc とは別インスタンス）
        auto probe = std::make_unique<QPdfDocument>();
        QPdfDocument::Error error = probe->load(path);
        QString password;
        if (error == QPdfDocument::Error::IncorrectPassword) {
            // パスワードは 1 回だけ問う。キャンセルなら失敗として記録
            bool entered = false;
            password = QInputDialog::getText(this, tr("パスワード"),
                tr("%1 のパスワード:").arg(QFileInfo(path).fileName()),
                QLineEdit::Password, QString(), &entered);
            if (!entered) {
                failures.push_back({path, 0, tr("パスワード入力がキャンセルされました")});
                continue;
            }
            probe->setPassword(password);
            error = probe->load(path);
        }
        if (error != QPdfDocument::Error::None) {
            failures.push_back({path, 0, tr("PDF として読み込めません")});
            continue;
        }
        const auto pages = parsePageRange(m_pageRangeEdit->text(), probe->pageCount());
        if (!pages) {
            failures.push_back({path, 0, tr("ページ範囲指定が不正です: %1")
                                             .arg(m_pageRangeEdit->text())});
            continue;
        }
        ConversionJob job;
        job.inputPdfPath = path;
        job.outputDirPath = outDir;
        job.pages = *pages;
        job.dpi = dpi;
        job.overwritePolicy = policy;
        job.password = password;
        jobs.push_back(std::move(job));
    }
    return jobs;
}

void MainWindow::startConversion() {
    if (m_fileList->count() == 0) {
        m_progressLabel->setText(tr("入力 PDF がありません"));
        return;
    }
    const QFileInfo outInfo(m_outputDirEdit->text());
    if (m_outputDirEdit->text().isEmpty() || !outInfo.isDir() || !outInfo.isWritable()) {
        m_progressLabel->setText(tr("出力先が書き込み可能なディレクトリではありません"));
        return;
    }
    // startConversion() 呼び出しごとにクリア。run() が finished を emit する
    // （onFinished() が showSummary() を呼ぶ）までメンバとして保持する
    // （Fix round 1, finding 1: ローカル変数だとスコープを抜けて消え、
    // 最終サマリから事前検査の失敗が黙って消える）。
    m_upfrontFailures.clear();
    const auto jobs = buildJobs(m_upfrontFailures);
    if (jobs.empty()) {
        ConversionSummary empty;
        showSummary(empty, m_upfrontFailures);
        return;
    }
    m_summaryView->clear();
    for (const PageFailure& f : m_upfrontFailures) {
        m_summaryView->appendPlainText(tr("[失敗] %1: %2").arg(f.filePath, f.reason));
    }
    setRunning(true);
    m_fileCount = static_cast<int>(jobs.size());
    // Task 6 レビューの修正により Converter::run() は cancel フラグを自分でリセット
    // しなくなった（run() が実際にワーカーで動き出す前に届いた requestCancel() を
    // 取りこぼさないため）。そのため「前回バッチのキャンセル状態を次のバッチへ
    // 持ち越さない」責務は呼び出し側が emit の直前に resetCancel() を呼ぶことで
    // 果たす。ここを省略すると、一度キャンセルした後の 2 回目以降の変換が
    // 「開始した瞬間に cancelled=true・ファイル 0 件」で即終了する
    // （クラッシュもエラーも出ない静かな不具合になる）。
    m_converter->resetCancel();
    emit conversionRequested(jobs);
}

void MainWindow::onFileStarted(int fileIndex, int fileCount, const QString& filePath) {
    m_currentFileIndex = fileIndex;
    m_fileCount = fileCount;
    m_progressLabel->setText(tr("ファイル %1/%2: %3")
        .arg(fileIndex).arg(fileCount).arg(QFileInfo(filePath).fileName()));
}

void MainWindow::onPageDone(int done, int total) {
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(done);
    // 進捗はテキストでも示す（アクセシビリティ要件）
    m_progressLabel->setText(tr("ファイル %1/%2: %3 ページ中 %4 ページ完了")
        .arg(m_currentFileIndex).arg(m_fileCount).arg(total).arg(done));
}

void MainWindow::onFinished(const ConversionSummary& summary) {
    setRunning(false);
    // {} ではなく m_upfrontFailures を渡す（Fix round 1, finding 1）。
    showSummary(summary, m_upfrontFailures);
}

void MainWindow::setRunning(bool running) {
    m_convertButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
    m_addButton->setEnabled(!running);
    m_removeButton->setEnabled(!running);
    if (running) {
        m_progressBar->setValue(0);
        m_progressLabel->setText(tr("変換を開始しました"));
    }
}

void MainWindow::showSummary(const ConversionSummary& summary,
                             const std::vector<PageFailure>& upfrontFailures) {
    QStringList lines;
    if (summary.cancelled) {
        lines << tr("キャンセルされました（途中までの PNG は残っています）");
    }
    if (summary.aborted) {
        lines << tr("出力先エラーのため変換を中止しました");
    }
    lines << tr("成功: %1 ページ / 失敗: %2 件 / スキップ: %3 ページ")
                 .arg(summary.succeededPages)
                 .arg(summary.failedPages + static_cast<int>(upfrontFailures.size()))
                 .arg(summary.skippedPages);
    for (const PageFailure& f : upfrontFailures) {
        lines << tr("[失敗] %1: %2").arg(f.filePath, f.reason);
    }
    for (const PageFailure& f : summary.failures) {
        if (f.pageNumber > 0) {
            lines << tr("[失敗] %1 の %2 ページ目: %3")
                         .arg(f.filePath).arg(f.pageNumber).arg(f.reason);
        } else {
            lines << tr("[失敗] %1: %2").arg(f.filePath, f.reason);
        }
    }
    m_summaryView->setPlainText(lines.join(u'\n'));
    // 一目で読む状態表示も実際の結果を反映する（Fix round 1, finding 3）。
    // キャンセル・中止のときに「完了」と表示しない。
    if (summary.cancelled) {
        m_progressLabel->setText(tr("キャンセルされました"));
    } else if (summary.aborted) {
        m_progressLabel->setText(tr("中止しました"));
    } else {
        m_progressLabel->setText(tr("完了"));
    }
}

} // namespace utsushi
