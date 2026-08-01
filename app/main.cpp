#include "main_window.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Utsushi"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    utsushi::MainWindow window;
    window.show();
    return QApplication::exec();
}
