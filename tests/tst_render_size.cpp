#include <QtTest>
#include "core/render_size.hpp"

using utsushi::renderSizeFor;

class TstRenderSize : public QObject {
    Q_OBJECT
private slots:
    void compute_data() {
        QTest::addColumn<QSizeF>("pagePt");
        QTest::addColumn<double>("dpi");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QSize>("expected");

        const QSizeF a4{595.276, 841.890};   // A4 (pt)
        QTest::newRow("a4@72")       << a4 << 72.0     << true  << QSize(595, 842);
        QTest::newRow("a4@150")      << a4 << 150.0    << true  << QSize(1240, 1754);
        QTest::newRow("a4@300")      << a4 << 300.0    << true  << QSize(2480, 3508);
        QTest::newRow("a4@600")      << a4 << 600.0    << true  << QSize(4961, 7016);
        QTest::newRow("min-1px")     << QSizeF(0.1, 0.1) << 72.0 << true << QSize(1, 1);
        QTest::newRow("edge-max")    << QSizeF(72.0, 72.0) << 20000.0 << true << QSize(20000, 20000);
        QTest::newRow("edge-over")   << QSizeF(72.0, 72.0) << 20001.0 << false << QSize();
        QTest::newRow("a4-huge-dpi") << a4 << 100000.0 << false << QSize();
        QTest::newRow("dpi-zero")    << a4 << 0.0      << false << QSize();
        QTest::newRow("dpi-negative")<< a4 << -300.0   << false << QSize();
        QTest::newRow("page-zero")   << QSizeF(0.0, 841.890) << 300.0 << false << QSize();
    }
    void compute() {
        QFETCH(QSizeF, pagePt);
        QFETCH(double, dpi);
        QFETCH(bool, valid);
        QFETCH(QSize, expected);

        const auto result = renderSizeFor(pagePt, dpi);
        QCOMPARE(result.has_value(), valid);
        if (valid) {
            QCOMPARE(*result, expected);
        }
    }
    void presetsAreSane() {
        QCOMPARE(utsushi::kStandardDpiPresets.size(), std::size_t{4});
        QCOMPARE(utsushi::kStandardDpiPresets.front(), utsushi::kScreenDpi);
    }
};

QTEST_APPLESS_MAIN(TstRenderSize)
#include "tst_render_size.moc"
