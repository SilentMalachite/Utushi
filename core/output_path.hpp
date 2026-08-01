#pragma once
#include <QString>

namespace utsushi {

// Windows の使用禁止文字 (\ / : * ? " < > |) と制御文字を '_' に置換し、
// NFC に正規化した stem を返す。前後の空白を除去し、空になったら "output"。
[[nodiscard]] QString sanitizedStem(const QString& stem);

// "{stem}_p{ゼロ埋めページ番号}.png" を返す。pageNumber は 1 始まり
// （PDF ビューアの表示と一致。0 始まりの内部インデックスと混同しない）。
// ゼロ埋め桁数は総ページ数の桁数、ただし最低 3 桁。stem は sanitizedStem を通す。
[[nodiscard]] QString outputFileName(const QString& stem, int pageNumber, int totalPages);

} // namespace utsushi
