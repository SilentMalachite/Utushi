#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/conversion_job.hpp"
#include "core/converter.hpp"
#include "core/render_size.hpp"

using namespace Qt::StringLiterals;
using utsushi::ConversionJob;
using utsushi::ConversionSummary;
using utsushi::Converter;
using utsushi::OverwritePolicy;

namespace {

// pageCount ページの正常な PDF を dir に生成してフルパスを返す。
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

// PDF として不正なファイルを生成してフルパスを返す。
QString writeBrokenPdf(const QString& dirPath, const QString& name) {
    const QString path = dirPath + u'/' + name;
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("this is not a pdf");
    return path;
}

ConversionSummary lastSummary(const QSignalSpy& spy) {
    return spy.last().first().value<ConversionSummary>();
}

} // namespace

class TstConverter : public QObject {
    Q_OBJECT
private slots:
    void convertsAllPagesAtGivenDpi() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdf = writeSamplePdf(dir.path(), u"sample.pdf"_s, 3);

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.dpi = 150.0;   // 全ページ・150dpi

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        QSignalSpy pageSpy(&converter, &Converter::pageDone);
        converter.run({job});

        QCOMPARE(finishedSpy.count(), 1);
        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.succeededPages, 3);
        QCOMPARE(summary.failedPages, 0);
        QVERIFY(!summary.cancelled);
        QCOMPARE(pageSpy.count(), 3);

        // 連番 3 桁・1 始まり。サイズは renderSizeFor と一致（ピクセル比較はしない）
        for (int page = 1; page <= 3; ++page) {
            const QString out = dir.path() + u"/sample_p%1.png"_s.arg(page, 3, 10, QLatin1Char('0'));
            QVERIFY2(QFileInfo::exists(out), qPrintable(out));
            const QImage img(out);
            const auto expected = utsushi::renderSizeFor(QSizeF(595.276, 841.890), 150.0);
            QVERIFY(expected.has_value());
            QCOMPARE(img.size(), *expected);
        }
    }

    void respectsPageSelection() {
        QTemporaryDir dir;
        const QString pdf = writeSamplePdf(dir.path(), u"sel.pdf"_s, 5);

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.pages = {2, 4};
        job.dpi = 150.0;

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({job});

        QCOMPARE(lastSummary(finishedSpy).succeededPages, 2);
        QVERIFY(!QFileInfo::exists(dir.path() + u"/sel_p001.png"_s));
        QVERIFY(QFileInfo::exists(dir.path() + u"/sel_p002.png"_s));
        QVERIFY(QFileInfo::exists(dir.path() + u"/sel_p004.png"_s));
    }
};

QTEST_MAIN(TstConverter)
#include "tst_converter.moc"
