#include "license_dialog.hpp"

#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace utsushi {

LicenseDialog::LicenseDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("ライセンス"));
    auto* layout = new QVBoxLayout(this);
    auto* text = new QPlainTextEdit(this);
    text->setReadOnly(true);
    text->setPlainText(tr(
        "Utsushi 0.1.0\n"
        "本アプリは GNU General Public License v3.0（GPLv3）で配布されます（同梱の LICENSE を参照）。\n"
        "\n"
        "本アプリは Qt 6（LGPLv3）を動的リンクで使用しています。\n"
        "Qt のソースコードは https://download.qt.io/official_releases/qt/ から入手できます。\n"
        "LGPLv3 の全文: https://www.gnu.org/licenses/lgpl-3.0.html\n"
        "\n"
        "Qt PDF は内部で PDFium（BSD-3-Clause）を使用しています。"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(text);
    layout->addWidget(buttons);
    resize(560, 360);
}

} // namespace utsushi
