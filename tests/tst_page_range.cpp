#include <QtTest>
#include "core/page_range.hpp"

using namespace Qt::StringLiterals;
using utsushi::parsePageRange;

class TstPageRange : public QObject {
    Q_OBJECT
private slots:
    void parse_data() {
        QTest::addColumn<QString>("spec");
        QTest::addColumn<int>("totalPages");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QList<int>>("expected");

        QTest::newRow("empty=all")      << u""_s        << 3  << true  << QList<int>{1, 2, 3};
        QTest::newRow("single")         << u"1"_s       << 10 << true  << QList<int>{1};
        QTest::newRow("range")          << u"1-5"_s     << 10 << true  << QList<int>{1, 2, 3, 4, 5};
        QTest::newRow("range+single")   << u"1-3,8"_s   << 10 << true  << QList<int>{1, 2, 3, 8};
        QTest::newRow("open-end")       << u"8-"_s      << 10 << true  << QList<int>{8, 9, 10};
        QTest::newRow("duplicates")     << u"2,2,1-3"_s << 10 << true  << QList<int>{1, 2, 3};
        QTest::newRow("unsorted-input") << u"8,1-3"_s   << 10 << true  << QList<int>{1, 2, 3, 8};
        QTest::newRow("spaces")         << u" 1 , 3 "_s << 10 << true  << QList<int>{1, 3};
        QTest::newRow("last-page")      << u"10"_s      << 10 << true  << QList<int>{10};
        QTest::newRow("reversed")       << u"5-1"_s     << 10 << false << QList<int>{};
        QTest::newRow("zero")           << u"0"_s       << 10 << false << QList<int>{};
        QTest::newRow("negative")       << u"-3"_s      << 10 << false << QList<int>{};
        QTest::newRow("alpha")          << u"abc"_s     << 10 << false << QList<int>{};
        QTest::newRow("over-total")     << u"11"_s      << 10 << false << QList<int>{};
        QTest::newRow("range-over")     << u"8-12"_s    << 10 << false << QList<int>{};
        QTest::newRow("trailing-comma") << u"1,"_s      << 10 << false << QList<int>{};
        QTest::newRow("total-zero")     << u"1"_s       << 0  << false << QList<int>{};
    }
    void parse() {
        QFETCH(QString, spec);
        QFETCH(int, totalPages);
        QFETCH(bool, valid);
        QFETCH(QList<int>, expected);

        const auto result = parsePageRange(spec, totalPages);
        QCOMPARE(result.has_value(), valid);
        if (valid) {
            QCOMPARE(QList<int>(result->begin(), result->end()), expected);
        }
    }
};

QTEST_APPLESS_MAIN(TstPageRange)
#include "tst_page_range.moc"
