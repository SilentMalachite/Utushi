#pragma once
#include <QDialog>

namespace utsushi {

// ヘルプ > ライセンス。GPLv3（本アプリ）と LGPLv3（Qt、動的リンク）の告知、
// および Qt ソース入手先の表示（受け入れ基準の必須項目）。
class LicenseDialog : public QDialog {
    Q_OBJECT
public:
    explicit LicenseDialog(QWidget* parent = nullptr);
};

} // namespace utsushi
