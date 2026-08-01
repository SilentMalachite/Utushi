#include "core/page_range.hpp"

#include <QList>
#include <QStringView>
#include <set>

namespace utsushi {
namespace {

// "12" → 12。数値以外・空・1 未満は nullopt。
std::optional<int> parsePageNumber(QStringView token) {
    bool ok = false;
    const int value = token.toInt(&ok);
    if (!ok || value < 1) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<std::vector<int>> parsePageRange(const QString& spec, int totalPages) {
    if (totalPages <= 0) {
        return std::nullopt;
    }
    const QString trimmed = spec.trimmed();
    std::set<int> pages;
    if (trimmed.isEmpty()) {
        for (int p = 1; p <= totalPages; ++p) {
            pages.insert(p);
        }
    } else {
        const auto parts = QStringView{trimmed}.split(u',');
        for (const auto& part : parts) {
            const QStringView token = part.trimmed();
            if (token.isEmpty()) {
                return std::nullopt;
            }
            const qsizetype dash = token.indexOf(u'-');
            if (dash < 0) {
                const auto page = parsePageNumber(token);
                if (!page || *page > totalPages) {
                    return std::nullopt;
                }
                pages.insert(*page);
            } else {
                const auto first = parsePageNumber(token.left(dash).trimmed());
                const QStringView lastToken = token.mid(dash + 1).trimmed();
                const auto last = lastToken.isEmpty()
                                      ? std::optional<int>{totalPages}
                                      : parsePageNumber(lastToken);
                if (!first || !last || *first > *last || *last > totalPages) {
                    return std::nullopt;
                }
                for (int p = *first; p <= *last; ++p) {
                    pages.insert(p);
                }
            }
        }
    }
    return std::vector<int>(pages.begin(), pages.end());
}

} // namespace utsushi
