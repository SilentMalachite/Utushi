#pragma once
#include <QString>
#include <optional>
#include <vector>

namespace utsushi {

// "1-5,8,11-" 形式のページ範囲指定を 1 始まりのページ番号列に解釈する。
// - spec が空（空白のみ）のとき: 全ページ [1..totalPages]
// - "N-" は N から最終ページまで
// - 重複は除去し昇順に整列する
// - 逆順("5-1")・0 以下・totalPages 超過・数値以外・totalPages <= 0 は std::nullopt
[[nodiscard]] std::optional<std::vector<int>> parsePageRange(const QString& spec,
                                                             int totalPages);

} // namespace utsushi
