#pragma once
#include <QSize>
#include <QSizeF>
#include <array>
#include <optional>

namespace utsushi {

// 出力画像の 1 辺の上限（px）。超える場合は変換を拒否し UI が「DPI が大きすぎます」と伝える。
inline constexpr int kMaxRenderEdgePx = 20000;

// DPI 換算定数と UI プリセット。リテラル 72 はこのファイルと render_size.cpp にのみ書ける
// （tst_no_hardcode が監視。DPI 定数の一元化）。
inline constexpr int kScreenDpi = 72;
inline constexpr std::array<int, 4> kStandardDpiPresets{kScreenDpi, 150, 300, 600};

// PDF のページサイズ（ポイント, 1pt = 1/72 inch）と DPI から出力ピクセルサイズを求める。
// px = round(points / 72.0 * dpi)。幅・高さとも最低 1px を保証する。
// 副作用なし。例外を投げない。dpi <= 0、ページ辺が 0 以下、
// 1 辺が kMaxRenderEdgePx 超のとき std::nullopt。
[[nodiscard]] std::optional<QSize> renderSizeFor(QSizeF pagePointSize, double dpi) noexcept;

} // namespace utsushi
