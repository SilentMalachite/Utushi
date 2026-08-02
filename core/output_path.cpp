#include "core/output_path.hpp"

#include <QRegularExpression>
#include <algorithm>

namespace utsushi {

QString sanitizedStem(const QString& stem) {
    static const QRegularExpression forbidden(
        QStringLiteral(R"([\\/:*?"<>|]|[\x00-\x1f])"));
    QString cleaned = stem.normalized(QString::NormalizationForm_C);
    cleaned.replace(forbidden, QStringLiteral("_"));
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty()) {
        return QStringLiteral("output");
    }
    return cleaned;
}

QString outputFileName(const QString& stem, int pageNumber, int totalPages) {
    // QString::number(int) の結果は高々 11 文字（INT_MIN は "-2147483648"）に収まる。
    // qsizetype -> int への変換は、この上限が成り立つこの一点でのみ安全に行える。
    const int totalPagesDigitCount =
        static_cast<int>(QString::number(totalPages).size());
    const int digits = std::max(3, totalPagesDigitCount);
    return QStringLiteral("%1_p%2.png")
        .arg(sanitizedStem(stem))
        .arg(pageNumber, digits, 10, QLatin1Char('0'));
}

} // namespace utsushi
