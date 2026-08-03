#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include "core/conversion_job.hpp"
#include "core/converter.hpp"
#include "core/render_size.hpp"

using namespace Qt::StringLiterals;
using utsushi::ConversionJob;
using utsushi::ConversionSummary;
using utsushi::Converter;
using utsushi::loadErrorText;
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
    if (!f.open(QIODevice::WriteOnly)) {
        return QString();
    }
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

    // 受け入れ基準「変換中にキャンセルすると 1 秒以内にワーカーが停止し、途中までの
    // PNG は残る」の時間境界そのものを実証する。cancelStopsAtPageBoundary() は run() を
    // テストと同じスレッドで呼ぶため「次のページへ進まない」ことしか示せず、UI から
    // キャンセルを押してからワーカーが実際に止まるまでの時間は測れない。ここでは
    // MainWindow と同じ構成（ワーカースレッドへ moveToThread した Converter に、別
    // スレッドから std::atomic 経由で requestCancel）で、その所要時間を測る。
    void cancelFromAnotherThreadStopsWithinOneSecond() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // キャンセルを送る時点でまだ大量のページが残っている状態にする。全ページが
        // 終わってしまうと cancelled が立たず、時間も測れない。
        const QString pdf = writeSamplePdf(dir.path(), u"many.pdf"_s, 100);

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.dpi = 150.0;

        QThread worker;
        // MainWindow と同じ所有権の受け渡し: 親を持たせず moveToThread し、スレッド
        // 終了時に deleteLater でワーカースレッド側から破棄する。
        auto* converter = new Converter;
        converter->moveToThread(&worker);
        connect(&worker, &QThread::finished, converter, &QObject::deleteLater);
        QSignalSpy pageSpy(converter, &Converter::pageDone);
        QSignalSpy finishedSpy(converter, &Converter::finished);
        worker.start();

        // QVERIFY 系はアサート失敗で即 return するため、その経路でもワーカーを必ず
        // 止める。走ったままの QThread が破棄されると qFatal でテストバイナリ全体が
        // 落ち、後続のテストが 1 本も実行されなくなる。
        // converter はスレッド終了時に deleteLater で破棄されるので、requestCancel()
        // は quit()/wait() より先に呼ぶ（このガードより後に converter へ触らない）。
        struct WorkerStopper {
            QThread& thread;
            Converter* converter;
            ~WorkerStopper() {
                converter->requestCancel();
                thread.quit();
                thread.wait();
            }
        } stopper{worker, converter};

        QMetaObject::invokeMethod(converter, [converter, job] { converter->run({job}); });

        // 実際に変換が始まってからキャンセルする。開始前のキャンセルは
        // cancelRequestedBeforeRunIsHonored() が別途カバーしている。
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.count() > 0, 15000);

        QElapsedTimer sinceCancel;
        sinceCancel.start();
        converter->requestCancel();
        QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 5000);
        const qint64 stopMs = sinceCancel.elapsed();

        const auto summary = lastSummary(finishedSpy);
        QVERIFY(summary.cancelled);
        QVERIFY2(stopMs < 1000, qPrintable(u"停止までに %1 ms かかった"_s.arg(stopMs)));
        QVERIFY(QFileInfo::exists(dir.path() + u"/many_p001.png"_s));   // 途中結果は残る
        QVERIFY(!QFileInfo::exists(dir.path() + u"/many_p100.png"_s));
    }

    // Fix wave 2, finding 1: 別ディレクトリにある同名 stem の 2 ファイルが同じ出力先へ
    // 変換されるとき、無言でスキップするのではなく明示的な衝突として報告すること。
    void duplicateStemAcrossInputDirsIsReportedAsConflict() {
        QTemporaryDir dirA;
        QTemporaryDir dirB;
        QTemporaryDir outDir;
        QVERIFY(dirA.isValid());
        QVERIFY(dirB.isValid());
        QVERIFY(outDir.isValid());
        const QString pdfA = writeSamplePdf(dirA.path(), u"report.pdf"_s, 1);
        const QString pdfB = writeSamplePdf(dirB.path(), u"report.pdf"_s, 1);

        ConversionJob jobA;
        jobA.inputPdfPath = pdfA;
        jobA.outputDirPath = outDir.path();
        jobA.dpi = 150.0;
        ConversionJob jobB;
        jobB.inputPdfPath = pdfB;
        jobB.outputDirPath = outDir.path();
        jobB.dpi = 150.0;
        // overwritePolicy は既定 (Skip) のまま

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({jobA, jobB});

        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.succeededPages, 1);   // 1 件目だけが変換される
        QCOMPARE(summary.skippedPages, 0);     // "スキップ" ではなく明示的な失敗として扱う
        QCOMPARE(summary.failedPages, 1);
        QCOMPARE(summary.failures.size(), std::size_t{1});
        QCOMPARE(summary.failures.front().filePath, pdfB);
        QCOMPARE(summary.failures.front().pageNumber, 0);
        QVERIFY2(summary.failures.front().reason.contains(pdfA),
                 qPrintable(summary.failures.front().reason));   // どのファイルと衝突したか分かる
        QVERIFY(QFileInfo::exists(outDir.path() + u"/report_p001.png"_s));
    }

    // Fix wave 2, finding 1 (Overwrite): 衝突する 2 件目は Overwrite でも処理されず、
    // 1 件目の出力を同一バッチ内で破壊しないこと。
    void duplicateStemConflictPreventsOverwriteDestroyingFirstOutput() {
        QTemporaryDir dirA;
        QTemporaryDir dirB;
        QTemporaryDir outDir;
        QVERIFY(dirA.isValid());
        QVERIFY(dirB.isValid());
        QVERIFY(outDir.isValid());
        const QString pdfA = writeSamplePdf(dirA.path(), u"report.pdf"_s, 2);
        const QString pdfB = writeSamplePdf(dirB.path(), u"report.pdf"_s, 3);

        ConversionJob jobA;
        jobA.inputPdfPath = pdfA;
        jobA.outputDirPath = outDir.path();
        jobA.dpi = 150.0;
        jobA.overwritePolicy = OverwritePolicy::Overwrite;
        ConversionJob jobB;
        jobB.inputPdfPath = pdfB;
        jobB.outputDirPath = outDir.path();
        jobB.dpi = 150.0;
        jobB.overwritePolicy = OverwritePolicy::Overwrite;

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({jobA, jobB});

        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.succeededPages, 2);   // jobA の 2 ページのみ
        QCOMPARE(summary.failedPages, 1);
        QVERIFY(QFileInfo::exists(outDir.path() + u"/report_p001.png"_s));
        QVERIFY(QFileInfo::exists(outDir.path() + u"/report_p002.png"_s));
        QVERIFY(!QFileInfo::exists(outDir.path() + u"/report_p003.png"_s));   // jobB は処理されていない
    }

    // Fix wave 2, re-review: 衝突判定は sanitizedStem() 後の値で行うこと。
    // "a:b.pdf" と "a?b.pdf" は生の completeBaseName() では別物 ("a:b" != "a?b") だが、
    // どちらも sanitizedStem() で "a_b" に正規化され、outputFileName() は同じ
    // "a_b_p001.png" を生成する。生の stem だけを比較すると、この衝突をすり抜ける。
    void sanitizedStemCollisionIsReportedAsConflict() {
#ifdef Q_OS_WIN
        // ':' と '?' は Windows（NTFS）自体が使用禁止文字としている集合と完全に一致する
        // （sanitizedStem() が置換する文字はまさに「Windows の使用禁止文字」）。そのため
        // このテストが検証したい「生の completeBaseName() は異なるが実在するファイルで、
        // sanitizedStem() を通すと同じ値に丸められる」状況そのものを、Windows では実ファイル
        // として構築できない（該当パスの作成が OS 側で拒否される）。この衝突検出の一般ロジック
        // は caseOnlyStemCollisionIsReportedAsConflict / duplicateStemAcrossInputDirsIsReportedAsConflict
        // など、Windows でも実ファイルとして再現できる他のテストでカバーされている。
        QSKIP("Windows では ':' '?' を含むファイル名を作成できないため対象外");
#endif
        QTemporaryDir dirA;
        QTemporaryDir dirB;
        QTemporaryDir outDir;
        QVERIFY(dirA.isValid());
        QVERIFY(dirB.isValid());
        QVERIFY(outDir.isValid());
        const QString pdfA = writeSamplePdf(dirA.path(), u"a:b.pdf"_s, 1);
        const QString pdfB = writeSamplePdf(dirB.path(), u"a?b.pdf"_s, 1);
        QVERIFY2(QFileInfo::exists(pdfA), qPrintable(pdfA));
        QVERIFY2(QFileInfo::exists(pdfB), qPrintable(pdfB));

        ConversionJob jobA;
        jobA.inputPdfPath = pdfA;
        jobA.outputDirPath = outDir.path();
        jobA.dpi = 150.0;
        ConversionJob jobB;
        jobB.inputPdfPath = pdfB;
        jobB.outputDirPath = outDir.path();
        jobB.dpi = 150.0;

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({jobA, jobB});

        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.succeededPages, 1);
        QCOMPARE(summary.skippedPages, 0);   // "スキップ" ではなく明示的な失敗として扱う
        QCOMPARE(summary.failedPages, 1);
        QCOMPARE(summary.failures.size(), std::size_t{1});
        QCOMPARE(summary.failures.front().filePath, pdfB);
        QVERIFY(QFileInfo::exists(outDir.path() + u"/a_b_p001.png"_s));
    }

    // Fix wave 2, re-review: macOS (APFS 既定) と Windows (NTFS 既定) はどちらも
    // 大文字小文字を区別しないファイルシステムが既定なので、"Report.pdf" と
    // "report.pdf" は同じファイルに書き込まれる。比較キーは大文字小文字を区別しない
    // こと（大文字小文字だけが異なる生の stem は別々のキーとして素通りしてはいけない）。
    void caseOnlyStemCollisionIsReportedAsConflict() {
        QTemporaryDir dirA;
        QTemporaryDir dirB;
        QTemporaryDir outDir;
        QVERIFY(dirA.isValid());
        QVERIFY(dirB.isValid());
        QVERIFY(outDir.isValid());
        const QString pdfA = writeSamplePdf(dirA.path(), u"Report.pdf"_s, 1);
        const QString pdfB = writeSamplePdf(dirB.path(), u"report.pdf"_s, 1);

        ConversionJob jobA;
        jobA.inputPdfPath = pdfA;
        jobA.outputDirPath = outDir.path();
        jobA.dpi = 150.0;
        ConversionJob jobB;
        jobB.inputPdfPath = pdfB;
        jobB.outputDirPath = outDir.path();
        jobB.dpi = 150.0;

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({jobA, jobB});

        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.succeededPages, 1);
        QCOMPARE(summary.skippedPages, 0);   // "スキップ" ではなく明示的な失敗として扱う
        QCOMPARE(summary.failedPages, 1);
        QCOMPARE(summary.failures.size(), std::size_t{1});
        QCOMPARE(summary.failures.front().filePath, pdfB);
    }

    // Fix wave 2, re-review: 同じファイルが一覧に重複して追加された場合、失敗理由が
    // 「自分自身と重複」という不自然な文言にならないこと。
    void duplicateEntryOfSameFileUsesSelfDuplicateWording() {
        QTemporaryDir dir;
        QTemporaryDir outDir;
        QVERIFY(dir.isValid());
        QVERIFY(outDir.isValid());
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 1);

        ConversionJob jobA;
        jobA.inputPdfPath = pdf;
        jobA.outputDirPath = outDir.path();
        jobA.dpi = 150.0;
        ConversionJob jobB = jobA;   // 同じファイルを 2 回追加

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({jobA, jobB});

        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.succeededPages, 1);
        QCOMPARE(summary.failedPages, 1);
        QCOMPARE(summary.failures.size(), std::size_t{1});
        QVERIFY2(!summary.failures.front().reason.contains(pdf),
                 qPrintable(summary.failures.front().reason));
    }

    // Fix wave 2, finding 3: ページ失敗を挟んでも pageDone の最終値は plannedInFile に
    // 到達すること。2 ページの PDF に対して {1, 2, 99} を指定すると 99 は範囲外で
    // 失敗するが、それも「試みた」うちに数えられ、プログレスバーが 3/3 で終わる
    // （2/3 で止まったまま UI 側だけ「完了」と表示される矛盾を防ぐ）。
    void progressReachesTotalEvenWhenAPageFails() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 2);

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.pages = {1, 2, 99};   // 99 はこの 2 ページ PDF では範囲外
        job.dpi = 150.0;

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        QSignalSpy pageSpy(&converter, &Converter::pageDone);
        converter.run({job});

        QCOMPARE(pageSpy.count(), 3);
        const auto lastArgs = pageSpy.last();
        QCOMPARE(lastArgs.at(0).toInt(), 3);   // done
        QCOMPARE(lastArgs.at(1).toInt(), 3);   // plannedInFile

        const auto summary = lastSummary(finishedSpy);
        QCOMPARE(summary.succeededPages, 2);
        QCOMPARE(summary.failedPages, 1);
    }

    // Blocker fix (2026-08-02 review): 1 ファイルの PNG 保存失敗はページ単位の失敗として
    // 記録し、後続の正常なファイルの変換を止めないこと。出力先ディレクトリ自体の
    // 存在・書き込み可否は別の検査（abortedOutputDirectoryCountsAsFailedPage）で
    // 既にカバーされているため、ここでは「ディレクトリは正常だが特定の出力パスへの
    // 保存だけが失敗する」状況を作る。出力予定のパスにあらかじめディレクトリを
    // 作っておくと、image.save() はそこへファイルとして書き込めず確実に失敗する
    // （クロスプラットフォームで再現可能。パーミッション操作より確実）。
    void saveFailureDoesNotAbortFollowingFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString blockedPdf = writeSamplePdf(dir.path(), u"blocked.pdf"_s, 1);
        const QString goodPdf = writeSamplePdf(dir.path(), u"good.pdf"_s, 1);
        // 保存先を先にディレクトリとして占有しておく。Overwrite を指定しない限り
        // Skip/Rename の exists() 分岐で吸収されてしまい、保存失敗そのものに
        // 到達できないため、ここでは Overwrite で直接 image.save() 失敗の経路を通す。
        QVERIFY(QDir(dir.path()).mkdir(u"blocked_p001.png"_s));

        ConversionJob blockedJob;
        blockedJob.inputPdfPath = blockedPdf;
        blockedJob.outputDirPath = dir.path();
        blockedJob.overwritePolicy = OverwritePolicy::Overwrite;
        ConversionJob goodJob;
        goodJob.inputPdfPath = goodPdf;
        goodJob.outputDirPath = dir.path();

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({blockedJob, goodJob});

        const auto summary = lastSummary(finishedSpy);
        QVERIFY(!summary.aborted);
        QCOMPARE(summary.failedPages, 1);
        QCOMPARE(summary.succeededPages, 1);
        QVERIFY(!summary.failures.empty());
        QVERIFY2(!summary.failures.front().reason.isEmpty(),
                 qPrintable(summary.failures.front().reason));
        QVERIFY(QFileInfo::exists(dir.path() + u"/good_p001.png"_s));
    }

    // Blocker fix (2026-08-02 review): Skip/Rename の保存先確保を「存在確認してから
    // 書く」方式から「排他的に確保してから書く」方式へ変える際、確保直後の書き込みが
    // 失敗したら、確保のために作られた 0 バイトのファイルを残さないこと。
    // この失敗経路は Converter::run() の通常経路からは到達できない
    // （image.isNull() がこの手前で既に弾くため、null 画像で run() を通してこの分岐へ
    // 入ることができない）。そのため保存とクリーンアップを担う writeImageExclusive() を
    // 直接呼び、意図的に null な QImage を渡して保存失敗を確定的に起こす。
    // これはタイミング競合のテストではない。何度実行しても同じ結果になる。
    void exclusiveWriteRemovesStrayFileOnSaveFailure() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + u"/stray.png"_s;

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QVERIFY2(QFileInfo::exists(path), "NewOnly のオープン自体が 0 バイトのファイルを作るはず");

        const QImage nullImage;   // isNull() == true。image.save() は必ず false を返す
        QVERIFY(nullImage.isNull());

        QVERIFY(!utsushi::writeImageExclusive(file, nullImage));
        QVERIFY2(!QFileInfo::exists(path),
                 "保存失敗時、確保のために作られた 0 バイトのファイルを残してはいけない");
    }

    // Blocker fix (2026-08-02 review): Rename の候補選定が「確認してから書く」方式でも
    // 複数の候補が既に埋まっている場合に正しく次の空き番号へ進むこと。これは競合を
    // 起こさない決定的なテストで、確保方式を「排他オープンで確保」へ変えたあとも
    // この観測可能な契約が壊れていないことを固定する。
    void renameSkipsTakenCandidatesToFindNextFree() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdf = writeSamplePdf(dir.path(), u"doc.pdf"_s, 1);

        const auto writeSentinel = [](const QString& path, const QByteArray& content) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(content);
        };
        const QString base = dir.path() + u"/doc_p001.png"_s;
        const QString taken2 = dir.path() + u"/doc_p001_2.png"_s;
        writeSentinel(base, "sentinel-base");
        writeSentinel(taken2, "sentinel-2");

        ConversionJob job;
        job.inputPdfPath = pdf;
        job.outputDirPath = dir.path();
        job.dpi = 150.0;
        job.overwritePolicy = OverwritePolicy::Rename;

        Converter converter;
        QSignalSpy finishedSpy(&converter, &Converter::finished);
        converter.run({job});

        QCOMPARE(lastSummary(finishedSpy).succeededPages, 1);
        QVERIFY(QFileInfo::exists(dir.path() + u"/doc_p001_3.png"_s));
        QFile checkBase(base);
        QVERIFY(checkBase.open(QIODevice::ReadOnly));
        QCOMPARE(checkBase.readAll(), QByteArray("sentinel-base"));
        QFile check2(taken2);
        QVERIFY(check2.open(QIODevice::ReadOnly));
        QCOMPARE(check2.readAll(), QByteArray("sentinel-2"));
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

    // パスワード保護 PDF は非対応スコープ（開発者判断）: QPdfWriter は暗号化 PDF を
    // 作れないため、この経路のフィクスチャは作れず一度も実行されたことがない。
    // 実行証跡ゼロの機能はリスクでしかないため、パスワード再入力の機能そのものを
    // 落とし、「非対応」という明確な失敗として扱う。ただし失われてはいけない
    // 振る舞いが 2 つある: (1) IncorrectPassword と UnsupportedSecurityScheme は
    // どちらも「非対応」の同じ文言に落ちる、(2) その文言は壊れたファイル用の
    // 汎用文言（「PDF として読み込めません」）とは異なる。汎用文言のままだと、
    // パスワード保護されているだけの正常なファイルが「壊れている」と誤解される。
    void passwordProtectedPdfReportsNotSupportedNotGenericFailure() {
        const QString incorrectPasswordText =
            loadErrorText(QPdfDocument::Error::IncorrectPassword);
        const QString unsupportedSchemeText =
            loadErrorText(QPdfDocument::Error::UnsupportedSecurityScheme);
        const QString genericUnreadableText =
            loadErrorText(QPdfDocument::Error::InvalidFileFormat);

        QCOMPARE(incorrectPasswordText, unsupportedSchemeText);
        QVERIFY2(incorrectPasswordText != genericUnreadableText,
                 qPrintable(incorrectPasswordText));
        QVERIFY2(incorrectPasswordText.contains(u"パスワード"_s),
                 qPrintable(incorrectPasswordText));
    }
};

QTEST_MAIN(TstConverter)
#include "tst_converter.moc"
