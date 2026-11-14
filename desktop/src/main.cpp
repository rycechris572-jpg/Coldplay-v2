
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QTimer>
#include <QByteArray>
#include <QLocalSocket>
#include <QPlainTextEdit>
#include <QDateTime>
#include <QStyle>
#include <array>

class ColdplayWindow : public QMainWindow {
public:
    ColdplayWindow() {
        setWindowTitle("COLDPLAY");
        resize(1080, 720);
        setMinimumSize(900, 620);

        buildUI();
        applyStyle();

        connect(&socket, &QLocalSocket::connected, this, [this] {
            setConnection("Connected · Handshaking", "#e8b45b");
        });

        connect(&socket, &QLocalSocket::readyRead, this, [this] {
            incoming += socket.readAll();
            processMessages();
        });

        connect(&socket, &QLocalSocket::disconnected, this, [this] {
            connected = false;
            setConnection("Disconnected · Reconnecting", "#e06c75");
            log("Bridge disconnected.");

            if (!reconnect.isActive())
                reconnect.start(1200);
        });

        connect(&socket, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) {
                if (socket.state() == QLocalSocket::UnconnectedState) {
                    connected = false;
                    setConnection("Waiting for Geometry Dash", "#8b93a7");
                }
            });

        connect(&reconnect, &QTimer::timeout, this,
            [this] { connectBridge(); });

        reconnect.start(1200);
        connectBridge();
        refreshPresets();
    }

private:
    QLocalSocket socket;
    QTimer reconnect;
    QByteArray incoming;

    QLabel* connectionLabel{};
    QLabel* connectionDot{};
    QLabel* selectedLabel{};
    QLabel* normalLabel{};
    QLabel* waveLabel{};
    QLabel* feedback{};
    QPlainTextEdit* logs{};

    std::array<QPushButton*, 10> buttons{};
    int selected = 0;
    bool connected = false;

    const std::array<int, 10> normal{
        2, 4, 8, 16, 24, 32, 48, 54, 60, 0
    };

    const std::array<int, 10> wave{
        2, 4, 8, 8, 24, 32, 48, 54, 60, 0
    };

    void buildUI() {
        auto* central = new QWidget(this);
        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(30, 24, 30, 26);
        root->setSpacing(20);

        // HEADER
        auto* header = new QHBoxLayout();
        auto* brandColumn = new QVBoxLayout();

        auto* brand = new QLabel("COLDPLAY");
        brand->setObjectName("brand");

        auto* subtitle = new QLabel(
            "PREDICTIVE INPUT CONTROL CENTER"
        );
        subtitle->setObjectName("eyebrow");

        brandColumn->addWidget(brand);
        brandColumn->addWidget(subtitle);
        header->addLayout(brandColumn);
        header->addStretch();

        auto* connectionCard = new QFrame();
        connectionCard->setObjectName("connectionCard");

        auto* connectionLayout = new QHBoxLayout(connectionCard);
        connectionLayout->setContentsMargins(14, 10, 16, 10);

        connectionDot = new QLabel("●");
        connectionDot->setObjectName("connectionDot");

        connectionLabel = new QLabel("Waiting for Geometry Dash");
        connectionLabel->setObjectName("connectionText");

        connectionLayout->addWidget(connectionDot);
        connectionLayout->addWidget(connectionLabel);
        header->addWidget(connectionCard);

        root->addLayout(header);

        auto* divider = new QFrame();
        divider->setFrameShape(QFrame::HLine);
        divider->setObjectName("divider");
        root->addWidget(divider);

        // INTRO
        auto* intro = new QVBoxLayout();

        auto* title = new QLabel("Buffer control");
        title->setObjectName("sectionTitle");

        auto* description = new QLabel(
            "Choose a preset here. COLDPLAY sends it directly to Geometry Dash."
        );
        description->setObjectName("muted");

        intro->addWidget(title);
        intro->addWidget(description);
        root->addLayout(intro);

        // PRESET PANEL
        auto* panel = new QFrame();
        panel->setObjectName("panel");

        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(22, 20, 22, 22);
        panelLayout->setSpacing(17);

        auto* currentRow = new QHBoxLayout();
        auto* currentColumn = new QVBoxLayout();

        auto* caption = new QLabel("SELECTED PRESET");
        caption->setObjectName("eyebrow");

        selectedLabel = new QLabel("STAGE 1");
        selectedLabel->setObjectName("selectedPreset");

        currentColumn->addWidget(caption);
        currentColumn->addWidget(selectedLabel);
        currentRow->addLayout(currentColumn);
        currentRow->addStretch();

        auto* values = new QFrame();
        values->setObjectName("valuesCard");

        auto* valuesLayout = new QHBoxLayout(values);
        valuesLayout->setContentsMargins(18, 12, 18, 12);
        valuesLayout->setSpacing(26);

        auto* normalColumn = new QVBoxLayout();
        auto* normalCaption = new QLabel("NORMAL");
        normalCaption->setObjectName("eyebrow");

        normalLabel = new QLabel("2 ticks");
        normalLabel->setObjectName("valueText");

        normalColumn->addWidget(normalCaption);
        normalColumn->addWidget(normalLabel);

        auto* waveColumn = new QVBoxLayout();
        auto* waveCaption = new QLabel("WAVE");
        waveCaption->setObjectName("eyebrow");

        waveLabel = new QLabel("2 ticks");
        waveLabel->setObjectName("valueText");

        waveColumn->addWidget(waveCaption);
        waveColumn->addWidget(waveLabel);

        valuesLayout->addLayout(normalColumn);
        valuesLayout->addLayout(waveColumn);
        currentRow->addWidget(values);

        panelLayout->addLayout(currentRow);

        // PRESET BUTTONS
        auto* grid = new QGridLayout();
        grid->setSpacing(10);

        for (int i = 0; i < 10; ++i) {
            QString name = i == 9
                ? "OFF"
                : QString("STAGE %1").arg(i + 1);

            auto* button = new QPushButton(name);
            button->setObjectName("presetButton");
            button->setMinimumHeight(54);
            button->setCursor(Qt::PointingHandCursor);

            buttons[i] = button;

            connect(button, &QPushButton::clicked, this, [this, i] {
                choosePreset(i);
            });

            grid->addWidget(button, i / 5, i % 5);
        }

        panelLayout->addLayout(grid);

        feedback = new QLabel(
            "Select a stage to send it to Geometry Dash."
        );
        feedback->setObjectName("feedback");

        panelLayout->addWidget(feedback);
        root->addWidget(panel);

        // ACTIVITY LOG
        auto* logPanel = new QFrame();
        logPanel->setObjectName("panel");

        auto* logLayout = new QVBoxLayout(logPanel);
        logLayout->setContentsMargins(18, 16, 18, 16);

        auto* logHeader = new QHBoxLayout();

        auto* logTitle = new QLabel("Activity");
        logTitle->setObjectName("cardTitle");

        auto* clear = new QPushButton("CLEAR");
        clear->setObjectName("smallButton");

        connect(clear, &QPushButton::clicked, this, [this] {
            logs->clear();
        });

        logHeader->addWidget(logTitle);
        logHeader->addStretch();
        logHeader->addWidget(clear);

        logLayout->addLayout(logHeader);

        logs = new QPlainTextEdit();
        logs->setObjectName("logView");
        logs->setReadOnly(true);
        logs->setMaximumBlockCount(150);

        logLayout->addWidget(logs);
        root->addWidget(logPanel, 1);

        auto* footer = new QLabel(
            "COLDPLAY  ·  DESKTOP CONTROLS GEODE"
        );
        footer->setObjectName("footer");
        root->addWidget(footer);

        setCentralWidget(central);
    }

    void applyStyle() {
        setStyleSheet(R"(
            QMainWindow, QWidget {
                background: #10131a;
                color: #edf0f7;
                font-family: "Segoe UI";
                font-size: 10pt;
            }

            #brand {
                color: #f4f6fb;
                font-size: 25pt;
                font-weight: 800;
                letter-spacing: 2px;
            }

            #eyebrow {
                color: #8993a8;
                font-size: 8pt;
                font-weight: 700;
            }

            #muted {
                color: #9aa4b8;
                font-size: 9pt;
            }

            #sectionTitle {
                font-size: 19pt;
                font-weight: 700;
            }

            #connectionCard, #panel {
                background: #181d27;
                border: 1px solid #293142;
                border-radius: 12px;
            }

            #connectionText {
                color: #cbd2df;
                font-size: 9pt;
                font-weight: 600;
            }

            #connectionDot {
                color: #8b93a7;
            }

            #divider {
                background: #293142;
                border: none;
                max-height: 1px;
            }

            #selectedPreset {
                color: white;
                font-size: 22pt;
                font-weight: 800;
            }

            #valuesCard {
                background: #111722;
                border: 1px solid #293142;
                border-radius: 9px;
            }

            #valueText {
                color: #f2f4fa;
                font-size: 14pt;
                font-weight: 700;
            }

            #presetButton {
                background: #202838;
                border: 1px solid #354158;
                border-radius: 8px;
                color: #cbd3e2;
                font-size: 9pt;
                font-weight: 700;
                padding: 8px;
            }

            #presetButton:hover {
                background: #29364b;
                border-color: #7187ad;
                color: white;
            }

            #presetButton[selected="true"] {
                background: #344b70;
                border: 1px solid #8aa9df;
                color: white;
            }

            #feedback {
                color: #9aa4b8;
                font-size: 9pt;
            }

            #cardTitle {
                color: #edf0f7;
                font-size: 12pt;
                font-weight: 700;
            }

            #smallButton {
                background: #252e3d;
                color: #b9c3d5;
                border: 1px solid #354158;
                border-radius: 6px;
                padding: 5px 10px;
            }

            #logView {
                background: #111722;
                border: 1px solid #293142;
                border-radius: 7px;
                color: #aebbd0;
                font-family: Consolas;
                font-size: 9pt;
            }

            #footer {
                color: #667187;
                font-size: 8pt;
                font-weight: 700;
            }
        )");
    }

    void setConnection(const QString& text, const QString& color) {
        connectionLabel->setText(text);
        connectionDot->setStyleSheet(
            QString("color: %1;").arg(color)
        );
    }

    void log(const QString& message) {
        QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
        logs->appendPlainText(
            QString("[%1] %2").arg(time, message)
        );
    }

    void connectBridge() {
        if (socket.state() != QLocalSocket::UnconnectedState)
            return;

        setConnection("Connecting to Geometry Dash", "#e8b45b");
        incoming.clear();

        socket.connectToServer(
            QStringLiteral(R"(\\.\pipe\ColdplayBridge)"),
            QIODevice::ReadWrite
        );
    }

    QString presetName(int index) {
        return index == 9
            ? "Buffer OFF"
            : QString("Stage %1").arg(index + 1);
    }

    void processMessages() {
        while (incoming.contains('\n')) {
            int newline = incoming.indexOf('\n');
            QByteArray message = incoming.left(newline).trimmed();
            incoming.remove(0, newline + 1);

            if (message == "COLDPLAY_READY") {
                socket.write("HELLO\n");
                socket.flush();
            }
            else if (message == "HELLO_ACK") {
                connected = true;
                reconnect.stop();

                setConnection(
                    "Connected to Geometry Dash",
                    "#72d69a"
                );

                log("Bridge handshake successful.");
                feedback->setText(
                    "Connected. Choose a preset to control the in-game buffer."
                );
            }
            else if (message.startsWith("PRESET_QUEUED ")) {
                int index = message.mid(14).toInt();

                if (index >= 0 && index <= 9) {
                    log(QString("Preset command queued: %1")
                        .arg(presetName(index)));

                    feedback->setText(
                        QString("%1 queued by Geode.")
                        .arg(presetName(index))
                    );
                }
            }
            else if (message == "INVALID_PRESET") {
                log("Geode rejected an invalid preset.");
                feedback->setText("Preset rejected by Geode.");
            }
        }
    }

    void choosePreset(int index) {
        selected = index;
        refreshPresets();

        if (!connected ||
            socket.state() != QLocalSocket::ConnectedState) {
            feedback->setText(
                "Not connected. Preset selected locally, not sent."
            );

            log(QString("%1 selected locally; bridge offline.")
                .arg(presetName(index)));
            return;
        }

        QByteArray command =
            QString("SET_PRESET %1\n").arg(index).toUtf8();

        qint64 written = socket.write(command);
        socket.flush();

        if (written == command.size()) {
            feedback->setText(
                QString("Sending %1 to Geometry Dash...")
                .arg(presetName(index))
            );

            log(QString("Sent %1 to Geode.")
                .arg(presetName(index)));
        } else {
            feedback->setText("Could not send preset.");
            log("Failed to send preset command.");
        }
    }

    void refreshPresets() {
        selectedLabel->setText(
            selected == 9
                ? "BUFFER OFF"
                : QString("STAGE %1").arg(selected + 1)
        );

        normalLabel->setText(
            QString("%1 ticks").arg(normal[selected])
        );

        waveLabel->setText(
            QString("%1 ticks").arg(wave[selected])
        );

        for (int i = 0; i < 10; ++i) {
            buttons[i]->setProperty(
                "selected",
                i == selected
            );

            buttons[i]->style()->unpolish(buttons[i]);
            buttons[i]->style()->polish(buttons[i]);
        }
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QApplication::setApplicationName("COLDPLAY");
    QApplication::setOrganizationName("COLDPLAY");

    ColdplayWindow window;
    window.show();

    return app.exec();
}
