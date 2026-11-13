
#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QLocalSocket>
#include <QTimer>
#include <QByteArray>

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

    QLocalSocket socket;
    QTimer reconnectTimer;
    QByteArray incoming;

    const QString pipeName =
        QStringLiteral(R"(\\.\pipe\ColdplayBridge)");

    auto connectToBridge = [&]() {
        if (socket.state() == QLocalSocket::UnconnectedState) {
            status->setText(
                "COLDPLAY Desktop\n\nConnecting to Geometry Dash..."
            );

            incoming.clear();
            socket.connectToServer(pipeName, QIODevice::ReadWrite);
        }
    };

    QObject::connect(
        &socket,
        &QLocalSocket::connected,
        [&]() {
            status->setText(
                "COLDPLAY Desktop\n\nBridge connected. Handshaking..."
            );
        }
    );

    QObject::connect(
        &socket,
        &QLocalSocket::readyRead,
        [&]() {
            incoming.append(socket.readAll());

            while (incoming.contains('\n')) {
                int newline = incoming.indexOf('\n');
                QByteArray message = incoming.left(newline).trimmed();
                incoming.remove(0, newline + 1);

                if (message == "COLDPLAY_READY") {
                    socket.write("HELLO\n");
                    socket.flush();
                }
                else if (message == "HELLO_ACK") {
                    status->setText(
                        "COLDPLAY Desktop\n\n"
                        "Connected to Geometry Dash!\n"
                        "Bridge handshake successful."
                    );

                    reconnectTimer.stop();
                }
            }
        }
    );

    QObject::connect(
        &socket,
        &QLocalSocket::disconnected,
        [&]() {
            status->setText(
                "COLDPLAY Desktop\n\n"
                "Connection lost. Reconnecting..."
            );

            incoming.clear();

            if (!reconnectTimer.isActive())
                reconnectTimer.start(1000);
        }
    );

    QObject::connect(
        &socket,
        &QLocalSocket::errorOccurred,
        [&](QLocalSocket::LocalSocketError) {
            if (socket.state() == QLocalSocket::UnconnectedState) {
                status->setText(
                    "COLDPLAY Desktop\n\n"
                    "Waiting for Geometry Dash..."
                );
            }
        }
    );

    QObject::connect(
        &reconnectTimer,
        &QTimer::timeout,
        connectToBridge
    );

    reconnectTimer.start(1000);
    connectToBridge();

    window.show();

    return app.exec();
}
