#include <QApplication>
#include <QMainWindow>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Utsushi"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QMainWindow window;   // Task 8 で utsushi::MainWindow に差し替える
    window.setWindowTitle(QStringLiteral("Utsushi"));
    window.show();
    return QApplication::exec();
}
