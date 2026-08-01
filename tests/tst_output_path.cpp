#include <QtTest>
#include "core/output_path.hpp"

using namespace Qt::StringLiterals;
using utsushi::outputFileName;
using utsushi::sanitizedStem;

class TstOutputPath : public QObject {
    Q_OBJECT
private slots:
    void fileName_data() {
        QTest::addColumn<QString>("stem");
        QTest::addColumn<int>("page");
        QTest::addColumn<int>("total");
        QTest::addColumn<QString>("expected");

        // ゼロ埋め桁数 = 総ページ数の桁数、ただし最低 3 桁
        QTest::newRow("total-1")    << u"report"_s << 1   << 1    << u"report_p001.png"_s;
        QTest::newRow("total-9")    << u"report"_s << 9   << 9    << u"report_p009.png"_s;
        QTest::newRow("total-10")   << u"report"_s << 10  << 10   << u"report_p010.png"_s;
        QTest::newRow("total-120")  << u"report"_s << 7   << 120  << u"report_p007.png"_s;   // CLAUDE.md の例
        QTest::newRow("total-999")  << u"report"_s << 999 << 999  << u"report_p999.png"_s;
        QTest::newRow("total-1000") << u"report"_s << 7   << 1000 << u"report_p0007.png"_s;
        QTest::newRow("japanese")   << u"月次報告"_s << 1 << 5    << u"月次報告_p001.png"_s;
        QTest::newRow("spaces")     << u"my report"_s << 1 << 5   << u"my report_p001.png"_s;
        QTest::newRow("slash")      << u"a/b"_s     << 1  << 5    << u"a_b_p001.png"_s;
        QTest::newRow("win-chars")  << uR"(a:b*c?d)"_s << 1 << 5  << u"a_b_c_d_p001.png"_s;
    }
    void fileName() {
        QFETCH(QString, stem);
        QFETCH(int, page);
        QFETCH(int, total);
        QFETCH(QString, expected);
        QCOMPARE(outputFileName(stem, page, total), expected);
    }
    void stem_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("clean")      << u"report"_s << u"report"_s;
        QTest::newRow("backslash")  << uR"(a\b)"_s << u"a_b"_s;
        QTest::newRow("angle-pipe") << u"a<b>c|d"_s << u"a_b_c_d"_s;
        QTest::newRow("quote")      << u"a\"b"_s   << u"a_b"_s;
        QTest::newRow("empty")      << u""_s       << u"output"_s;
        QTest::newRow("only-bad")   << u"???"_s    << u"___"_s;
        // NFD（macOS のファイル名）を NFC に正規化する: "ポ" = ホ + 半濁点
        QTest::newRow("nfd-to-nfc") << QString(u"ポ"_s) << u"ポ"_s;
    }
    void stem() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(sanitizedStem(input), expected);
    }
};

QTEST_APPLESS_MAIN(TstOutputPath)
#include "tst_output_path.moc"
