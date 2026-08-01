#include <QtTest>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>
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

        // フィクスチャの実際のページサイズを読み取る。QPdfWriter + QPageSize::A4 の
        // 出力は理論値 595.276×841.890pt と厳密には一致しない（整数ポイントに
        // 丸められることがある）ため、ハードコードした理論値ではなく実測値を
        // 期待値の計算に使う（Fix round 1, finding 3）。300dpi は理論値と実測値の
        // 丸め結果が偶然一致しない DPI（150dpi では偶然一致してしまい、この
        // バグを検出できなかった）。
        QPdfDocument fixtureDoc;
        QCOMPARE(fixtureDoc.load(pdf), QPdfDocument::Error::None);
        const QSizeF actualPageSize = fixtureDoc.pagePointSize(0);

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.dpi = 300.0;   // 全ページ・300dpi

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
            const auto expected = utsushi::renderSizeFor(actualPageSize, job.dpi);
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

    // Fix round 1, finding 1: 出力先が書き込めずバッチを中止したとき、failures に
    // 理由が記録されるだけでなく failedPages にも数えられていること。
    // UI は failedPages を件数表示に、failures を詳細一覧に使うため、両者は一致しなければならない。
    void abortedOutputDirectoryCountsAsFailedPage() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdf = writeSamplePdf(dir.path(), u"sample.pdf"_s, 1);

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path() + u"/does-not-exist"_s;   // 出力先が存在しない

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({job});

        const auto summary = lastSummary(finishedSpy);
        QVERIFY(summary.aborted);
        QCOMPARE(summary.failures.size(), 1);
        QCOMPARE(summary.failedPages, 1);
        QCOMPARE(summary.succeededPages, 0);
        QCOMPARE(summary.skippedPages, 0);
    }

    // Fix round 1, finding 2 (前半): run() が始まる前に requestCancel() が届いていた場合、
    // その要求を握り潰さず、1 ファイルにも着手せずキャンセル終了すること。
    void cancelRequestedBeforeRunIsHonored() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdf1 = writeSamplePdf(dir.path(), u"a.pdf"_s, 2);
        const QString pdf2 = writeSamplePdf(dir.path(), u"b.pdf"_s, 2);

        ConversionJob job1;
        job1.inputPdfPath = pdf1;
        job1.outputDirPath = dir.path();
        ConversionJob job2;
        job2.inputPdfPath = pdf2;
        job2.outputDirPath = dir.path();

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        QSignalSpy fileStartedSpy(&converter, &Converter::fileStarted);

        converter.requestCancel();   // run() が始まる前に届いたキャンセル
        converter.run({job1, job2});

        QCOMPARE(finishedSpy.count(), 1);
        const auto summary = lastSummary(finishedSpy);
        QVERIFY(summary.cancelled);
        QCOMPARE(fileStartedSpy.count(), 0);   // どのファイルにも着手していない
        QCOMPARE(summary.succeededPages, 0);
    }

    // Fix round 1, finding 2 (後半): 1 ファイル目の処理完了後・2 ファイル目に着手する前に
    // キャンセルが要求された場合、2 ファイル目には着手せずキャンセル終了すること
    // （ファイル境界間でのキャンセルを模擬）。
    void cancelBetweenFilesIsHonored() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdf1 = writeSamplePdf(dir.path(), u"a.pdf"_s, 2);
        const QString pdf2 = writeSamplePdf(dir.path(), u"b.pdf"_s, 2);

        ConversionJob job1;
        job1.inputPdfPath = pdf1;
        job1.outputDirPath = dir.path();
        ConversionJob job2;
        job2.inputPdfPath = pdf2;
        job2.outputDirPath = dir.path();

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        QSignalSpy fileStartedSpy(&converter, &Converter::fileStarted);
        // 1 ファイル目の最終ページが完了した直後にキャンセルを要求する。
        connect(&converter, &Converter::pageDone, &converter, [&converter](int done, int total) {
            if (done == total) {
                converter.requestCancel();
            }
        });

        converter.run({job1, job2});

        QCOMPARE(finishedSpy.count(), 1);
        const auto summary = lastSummary(finishedSpy);
        QVERIFY(summary.cancelled);
        QCOMPARE(fileStartedSpy.count(), 1);   // 2 ファイル目には着手していない
        QVERIFY(!QFileInfo::exists(dir.path() + u"/b_p001.png"_s));
    }

    // 壊れた PDF を含む 3 ファイルのバッチ: 正常 2 件は変換され、失敗 1 件が理由付きで記録される
    void batchContinuesAfterBrokenFile() {
        QTemporaryDir dir;
        const QString good1 = writeSamplePdf(dir.path(), u"good1.pdf"_s, 2);
        const QString broken = writeBrokenPdf(dir.path(), u"broken.pdf"_s);
        const QString good2 = writeSamplePdf(dir.path(), u"good2.pdf"_s, 2);

        std::vector<ConversionJob> jobs;
        for (const QString& path : {good1, broken, good2}) {
            ConversionJob job;
            job.inputPdfPath = path;
            job.outputDirPath = dir.path();
            job.dpi = 150.0;
            jobs.push_back(job);
        }

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run(jobs);

        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.succeededPages, 4);          // good1 + good2
        QCOMPARE(summary.failures.size(), std::size_t{1});
        QCOMPARE(summary.failures.front().filePath, broken);
        QCOMPARE(summary.failures.front().pageNumber, 0);
        QVERIFY(!summary.failures.front().reason.isEmpty());
        QVERIFY(!summary.aborted);                    // 1 件の失敗で全体を止めない
    }

    // Skip ポリシー: 既存ファイルは上書きされず、中身が変わらない
    void skipPolicyDoesNotOverwrite() {
        QTemporaryDir dir;
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 1);
        const QString existing = dir.path() + u"/doc_p001.png"_s;
        QFile f(existing);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("sentinel");
        f.close();

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.dpi = 150.0;
        job.overwritePolicy = OverwritePolicy::Skip;   // 既定値でもあるが明示

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({job});

        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.skippedPages, 1);
        QCOMPARE(summary.succeededPages, 0);
        QFile check(existing);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("sentinel"));   // 中身が保存されている
    }

    // Rename ポリシー: 既存ファイルを残し _2 付きで保存する
    void renamePolicyAddsSuffix() {
        QTemporaryDir dir;
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 1);
        const QString existing = dir.path() + u"/doc_p001.png"_s;
        QFile f(existing);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("sentinel");
        f.close();

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.dpi = 150.0;
        job.overwritePolicy = OverwritePolicy::Rename;

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({job});

        QCOMPARE(lastSummary(finishedSpy).succeededPages, 1);
        QVERIFY(QFileInfo::exists(dir.path() + u"/doc_p001_2.png"_s));
        QFile check(existing);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("sentinel"));
    }

    // Overwrite ポリシー: 明示指定があるときだけ上書きされる
    void overwritePolicyReplacesFile() {
        QTemporaryDir dir;
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 1);
        const QString existing = dir.path() + u"/doc_p001.png"_s;
        QFile f(existing);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("sentinel");
        f.close();

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.dpi = 150.0;
        job.overwritePolicy = OverwritePolicy::Overwrite;

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({job});

        QCOMPARE(lastSummary(finishedSpy).succeededPages, 1);
        const QImage img(existing);
        QVERIFY(!img.isNull());   // PNG として読める = sentinel が置き換わった
    }

    // キャンセル: フラグが立っていればページ境界で停止し、途中までの PNG は残る
    // (ファイル内の 1 ページ目完了直後にキャンセルする = ファイル境界ではなく
    // ページ境界でのキャンセルを検証する。cancelBetweenFilesIsHonored とは別の経路)
    void cancelStopsAtPageBoundary() {
        QTemporaryDir dir;
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 3);

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.dpi = 150.0;

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        // 1 ページ目の完了時にキャンセル → 2 ページ目以降は処理されない
        connect(&converter, &Converter::pageDone, &converter,
                [&converter] { converter.requestCancel(); });
        converter.run({job});

        const auto summary = lastSummary(finishedSpy);
        QVERIFY(summary.cancelled);
        QCOMPARE(summary.succeededPages, 1);
        QVERIFY(QFileInfo::exists(dir.path() + u"/doc_p001.png"_s));   // 途中結果は残る
        QVERIFY(!QFileInfo::exists(dir.path() + u"/doc_p003.png"_s));
    }

    // DPI 過大: renderSizeFor が nullopt を返し、ページ失敗として記録される
    void oversizedDpiFailsPage() {
        QTemporaryDir dir;
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 1);

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.dpi = 100000.0;   // A4 では 20000px 上限を超える

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({job});

        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.failedPages, 1);
        QCOMPARE(summary.failures.front().pageNumber, 1);
    }
};

QTEST_MAIN(TstConverter)
#include "tst_converter.moc"
