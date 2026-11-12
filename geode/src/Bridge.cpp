
#include "Bridge.hpp"
#include "../../shared/protocol/Protocol.hpp"

#include <Windows.h>
#include <string>
#include <chrono>

void ColdplayBridge::start() {
    bool expected = false;

    if (!m_running.compare_exchange_strong(expected, true))
        return;

    m_worker = std::thread(&ColdplayBridge::workerLoop, this);
}

void ColdplayBridge::stop() {
    m_running.store(false);

    if (m_worker.joinable())
        m_worker.join();

    m_connected.store(false);
}

bool ColdplayBridge::isConnected() const {
    return m_connected.load();
}

ColdplayBridge::~ColdplayBridge() {
    stop();
}

void ColdplayBridge::workerLoop() {
    const std::string pipeName(Coldplay::Protocol::PIPE_NAME);

    while (m_running.load()) {
        HANDLE pipe = CreateNamedPipeA(
            pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
            1,
            Coldplay::Protocol::MAX_MESSAGE_SIZE,
            Coldplay::Protocol::MAX_MESSAGE_SIZE,
            0,
            nullptr
        );

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(500);
            continue;
        }

        bool connected = false;

        while (m_running.load()) {
            if (ConnectNamedPipe(pipe, nullptr)) {
                connected = true;
                break;
            }

            DWORD error = GetLastError();

            if (error == ERROR_PIPE_CONNECTED) {
                connected = true;
                break;
            }

            if (error != ERROR_PIPE_LISTENING &&
                error != ERROR_NO_DATA) {
                break;
            }

            Sleep(20);
        }

        if (connected && m_running.load()) {
            m_connected.store(true);

            const char greeting[] = "COLDPLAY_READY\n";
            DWORD written = 0;

            WriteFile(
                pipe,
                greeting,
                sizeof(greeting) - 1,
                &written,
                nullptr
            );

            std::string incoming;
            char buffer[512];

            while (m_running.load()) {
                DWORD bytesRead = 0;

                BOOL result = ReadFile(
                    pipe,
                    buffer,
                    sizeof(buffer),
                    &bytesRead,
                    nullptr
                );

                if (result && bytesRead > 0) {
                    incoming.append(buffer, bytesRead);

                    size_t newline;

                    while ((newline = incoming.find('\n'))
                           != std::string::npos) {
                        std::string command =
                            incoming.substr(0, newline);

                        incoming.erase(0, newline + 1);

                        std::string response;

                        if (command == "PING")
                            response = "PONG\n";
                        else if (command == "HELLO")
                            response = "HELLO_ACK\n";
                        else
                            response = "UNKNOWN_COMMAND\n";

                        DWORD responseBytes = 0;

                        if (!WriteFile(
                            pipe,
                            response.data(),
                            static_cast<DWORD>(response.size()),
                            &responseBytes,
                            nullptr
                        )) {
                            break;
                        }
                    }
                }
                else if (!result) {
                    DWORD error = GetLastError();

                    if (error == ERROR_NO_DATA) {
                        Sleep(10);
                        continue;
                    }

                    if (error == ERROR_MORE_DATA) {
                        incoming.append(buffer, bytesRead);
                        continue;
                    }

                    break;
                }
                else {
                    Sleep(10);
                }
            }
        }

        m_connected.store(false);

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    m_connected.store(false);
}
