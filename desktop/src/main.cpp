
#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("COLDPLAY");
    window.resize(1000, 650);

    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);

    auto* status = new QLabel(
        "COLDPLAY Desktop\n\nWaiting for Geometry Dash..."
    );

    layout->addWidget(status);
    window.setCentralWidget(central);

    window.show();

    return app.exec();
}
