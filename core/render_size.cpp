#include "core/render_size.hpp"

#include <algorithm>
#include <cmath>

namespace utsushi {

std::optional<QSize> renderSizeFor(QSizeF pagePointSize, double dpi) noexcept {
    if (!std::isfinite(dpi) || !std::isfinite(pagePointSize.width()) ||
        !std::isfinite(pagePointSize.height())) {
        return std::nullopt;
    }
    if (dpi <= 0.0 || pagePointSize.width() <= 0.0 || pagePointSize.height() <= 0.0) {
        return std::nullopt;
    }
    const double pointsPerInch = static_cast<double>(kScreenDpi);   // 1pt = 1/72 inch
    const auto toPixels = [&](double points) noexcept {
        return std::max(1.0, std::round(points / pointsPerInch * dpi));
    };
    const double w = toPixels(pagePointSize.width());
    const double h = toPixels(pagePointSize.height());
    if (w > kMaxRenderEdgePx || h > kMaxRenderEdgePx) {
        return std::nullopt;
    }
    return QSize(static_cast<int>(w), static_cast<int>(h));
}

} // namespace utsushi
