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
    const int digits =
        std::max<qsizetype>(3, QString::number(totalPages).size());
    return QStringLiteral("%1_p%2.png")
        .arg(sanitizedStem(stem))
        .arg(pageNumber, static_cast<int>(digits), 10, QLatin1Char('0'));
}

} // namespace utsushi
