#include <QtTest>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "app/main_window.hpp"

using namespace Qt::StringLiterals;
using utsushi::MainWindow;

namespace {

// pageCount ページの正常な PDF を dir に生成してフルパスを返す（tst_converter.cpp の
// writeSamplePdf と同内容。テスト実行ファイルは独立バイナリなので重複させる）。
QString writeSamplePdf(const QString& dirPath, const QString& name, int pageCount) {
    const QString path = dirPath + u'/' + name;
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&writer);
    for (int i = 0; i < pageCount; ++i) {
        if (i > 0) {
            writer.newPage();
        }
        painter.drawText(100, 100, u"page %1"_s.arg(i + 1));
    }
    painter.end();
    return path;
}

} // namespace

class TstMainWindow : public QObject {
    Q_OBJECT
private slots:
    // Fix wave 2, finding 2: Converter::run() は cancel フラグを自分でリセットしない
    // 契約になっている。MainWindow::startConversion() が emit の直前に
    // m_converter->resetCancel() を呼ぶ責務を怠ると、一度キャンセルした後の
    // 2 回目以降の変換が「開始した瞬間に cancelled=true・ファイル 0 件」で
    // 即終了する（クラッシュもエラーも出ない）。この mutation を検出できることが
    // このテストの存在意義そのもの。
    void cancelThenReconvertConvertsAgain() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 3);

        MainWindow window;
        auto* fileList = window.findChild<QListWidget*>(u"fileList"_s);
        auto* outputDirEdit = window.findChild<QLineEdit*>(u"outputDirEdit"_s);
        auto* convertButton = window.findChild<QPushButton*>(u"convertButton"_s);
        auto* cancelButton = window.findChild<QPushButton*>(u"cancelButton"_s);
        auto* summaryView = window.findChild<QPlainTextEdit*>(u"summaryView"_s);
        QVERIFY(fileList);
        QVERIFY(outputDirEdit);
        QVERIFY(convertButton);
        QVERIFY(cancelButton);
        QVERIFY(summaryView);

        fileList->addItem(pdf);
        outputDirEdit->setText(dir.path());

        // 1 回目: conversionRequested はワーカースレッドへのキュー接続なので、
        // convertButton->click() が返った時点ではまだワーカーに届いていない。
        // 直後に cancelButton をクリックすると requestCancel() が atomic フラグへ
        // 即座に反映され（Qt::DirectConnection）、run() が最初のファイル境界
        // チェックで検出して 1 ファイルにも着手せずキャンセル終了する。
        {
            QSignalSpy summarySpy(summaryView, &QPlainTextEdit::textChanged);
            convertButton->click();
            cancelButton->click();
            QVERIFY(summarySpy.wait(5000));
            QVERIFY2(summaryView->toPlainText().contains(u"キャンセル"_s),
                     qPrintable(summaryView->toPlainText()));
        }

        // 2 回目: resetCancel() が呼ばれていれば、フラグが持ち越されず正常に変換される。
        {
            QSignalSpy summarySpy(summaryView, &QPlainTextEdit::textChanged);
            convertButton->click();
            QVERIFY(summarySpy.wait(5000));
            QVERIFY2(summaryView->toPlainText().contains(u"成功: 3 ページ"_s),
                     qPrintable(summaryView->toPlainText()));
        }
    }

    // 事前検査（buildJobs）で弾かれた失敗と、ワーカー（Converter::run）側で発生した
    // ページ失敗が、showSummary() の「失敗: N 件」に両方合算されて出ること。
    void summaryCountsCombinePreflightAndWorkerFailures() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString goodPdf = writeSamplePdf(dir.path(), u"good.pdf"_s, 1);
        const QString missingPdf = dir.path() + u"/missing.pdf"_s;   // 存在しない

        MainWindow window;
        auto* fileList = window.findChild<QListWidget*>(u"fileList"_s);
        auto* outputDirEdit = window.findChild<QLineEdit*>(u"outputDirEdit"_s);
        auto* dpiCombo = window.findChild<QComboBox*>(u"dpiCombo"_s);
        auto* convertButton = window.findChild<QPushButton*>(u"convertButton"_s);
        auto* summaryView = window.findChild<QPlainTextEdit*>(u"summaryView"_s);
        QVERIFY(fileList);
        QVERIFY(outputDirEdit);
        QVERIFY(dpiCombo);
        QVERIFY(convertButton);
        QVERIFY(summaryView);

        fileList->addItem(missingPdf);   // buildJobs() の事前検査で弾かれる（upfront failure）
        fileList->addItem(goodPdf);      // buildJobs() は通るが、Converter::run() 側で失敗する
        outputDirEdit->setText(dir.path());
        dpiCombo->setCurrentText(u"100000"_s);   // A4 では 20000px 上限を超える

        QSignalSpy summarySpy(summaryView, &QPlainTextEdit::textChanged);
        convertButton->click();
        QVERIFY(summarySpy.wait(5000));

        const QString text = summaryView->toPlainText();
        QVERIFY2(text.contains(u"失敗: 2 件"_s), qPrintable(text));   // 事前検査 1 + ワーカー 1
        QVERIFY2(text.contains(missingPdf), qPrintable(text));
        QVERIFY2(text.contains(u"DPI が大きすぎます"_s), qPrintable(text));
    }

    // 既存ファイルへの挙動を通じて、overwriteCombo の選択が正しい OverwritePolicy に
    // マッピングされていることを検証する（Skip → Overwrite → Rename の順）。
    void overwriteComboControlsPolicy() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 1);
        const QString existing = dir.path() + u"/doc_p001.png"_s;

        MainWindow window;
        auto* fileList = window.findChild<QListWidget*>(u"fileList"_s);
        auto* outputDirEdit = window.findChild<QLineEdit*>(u"outputDirEdit"_s);
        auto* overwriteCombo = window.findChild<QComboBox*>(u"overwriteCombo"_s);
        auto* convertButton = window.findChild<QPushButton*>(u"convertButton"_s);
        auto* summaryView = window.findChild<QPlainTextEdit*>(u"summaryView"_s);
        QVERIFY(fileList);
        QVERIFY(outputDirEdit);
        QVERIFY(overwriteCombo);
        QVERIFY(convertButton);
        QVERIFY(summaryView);

        fileList->addItem(pdf);
        outputDirEdit->setText(dir.path());

        const auto writeSentinel = [&] {
            QFile f(existing);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("sentinel");
        };

        // 既定 (index 0 = Skip): 既存ファイルは変更されない
        writeSentinel();
        {
            QSignalSpy summarySpy(summaryView, &QPlainTextEdit::textChanged);
            convertButton->click();
            QVERIFY(summarySpy.wait(5000));
        }
        QVERIFY2(summaryView->toPlainText().contains(u"スキップ: 1 ページ"_s),
                 qPrintable(summaryView->toPlainText()));
        {
            QFile check(existing);
            QVERIFY(check.open(QIODevice::ReadOnly));
            QCOMPARE(check.readAll(), QByteArray("sentinel"));
        }

        // index 1 = Overwrite: 既存ファイルが置き換わる
        overwriteCombo->setCurrentIndex(1);
        {
            QSignalSpy summarySpy(summaryView, &QPlainTextEdit::textChanged);
            convertButton->click();
            QVERIFY(summarySpy.wait(5000));
        }
        {
            QFile check(existing);
            QVERIFY(check.open(QIODevice::ReadOnly));
            QVERIFY(check.readAll() != QByteArray("sentinel"));
        }

        // index 2 = Rename: 新しいファイルが _2 付きで作られる
        overwriteCombo->setCurrentIndex(2);
        {
            QSignalSpy summarySpy(summaryView, &QPlainTextEdit::textChanged);
            convertButton->click();
            QVERIFY(summarySpy.wait(5000));
        }
        QVERIFY(QFileInfo::exists(dir.path() + u"/doc_p001_2.png"_s));
    }
};

QTEST_MAIN(TstMainWindow)
#include "tst_main_window.moc"
