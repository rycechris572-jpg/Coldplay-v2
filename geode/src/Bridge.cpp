
#include "Bridge.hpp"
#include "../../shared/protocol/Protocol.hpp"

#include <Windows.h>
#include <string>

namespace {

bool finishOperation(
    HANDLE pipe,
    OVERLAPPED& operation,
    DWORD& transferred,
    const std::atomic<bool>& running
) {
    while (true) {
        DWORD wait = WaitForSingleObject(operation.hEvent, 50);

        if (wait == WAIT_OBJECT_0) {
            return GetOverlappedResult(
                pipe, &operation, &transferred, FALSE
            ) != FALSE;
        }

        if (wait == WAIT_FAILED)
            return false;

        if (!running.load()) {
            CancelIoEx(pipe, &operation);
            WaitForSingleObject(operation.hEvent, INFINITE);
            return false;
        }
    }
}

bool writeAll(
    HANDLE pipe,
    const char* data,
    DWORD size,
    const std::atomic<bool>& running
) {
    DWORD offset = 0;

    while (offset < size && running.load()) {
        OVERLAPPED operation{};
        operation.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

        if (!operation.hEvent)
            return false;

        DWORD written = 0;

        BOOL result = WriteFile(
            pipe,
            data + offset,
            size - offset,
            &written,
            &operation
        );

        if (!result && GetLastError() == ERROR_IO_PENDING) {
            result = finishOperation(
                pipe, operation, written, running
            );
        }

        CloseHandle(operation.hEvent);

        if (!result || written == 0)
            return false;

        offset += written;
    }

    return offset == size;
}

bool readSome(
    HANDLE pipe,
    char* buffer,
    DWORD capacity,
    DWORD& bytesRead,
    const std::atomic<bool>& running
) {
    OVERLAPPED operation{};
    operation.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    if (!operation.hEvent)
        return false;

    bytesRead = 0;

    BOOL result = ReadFile(
        pipe,
        buffer,
        capacity,
        &bytesRead,
        &operation
    );

    if (!result && GetLastError() == ERROR_IO_PENDING) {
        result = finishOperation(
            pipe, operation, bytesRead, running
        );
    }

    CloseHandle(operation.hEvent);

    return result && bytesRead > 0;
}

}

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
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
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

        OVERLAPPED connectOperation{};
        connectOperation.hEvent =
            CreateEventA(nullptr, TRUE, FALSE, nullptr);

        bool connected = false;

        if (connectOperation.hEvent) {
            BOOL result = ConnectNamedPipe(
                pipe, &connectOperation
            );

            if (result) {
                connected = true;
            }
            else {
                DWORD error = GetLastError();

                if (error == ERROR_PIPE_CONNECTED) {
                    connected = true;
                }
                else if (error == ERROR_IO_PENDING) {
                    DWORD transferred = 0;

                    connected = finishOperation(
                        pipe,
                        connectOperation,
                        transferred,
                        m_running
                    );
                }
            }

            CloseHandle(connectOperation.hEvent);
        }

        if (connected && m_running.load()) {
            m_connected.store(true);

            const char greeting[] = "COLDPLAY_READY\n";

            if (writeAll(
                pipe,
                greeting,
                sizeof(greeting) - 1,
                m_running
            )) {
                std::string incoming;
                char buffer[512];

                while (m_running.load()) {
                    DWORD bytesRead = 0;

                    if (!readSome(
                        pipe,
                        buffer,
                        sizeof(buffer),
                        bytesRead,
                        m_running
                    )) {
                        break;
                    }

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

                        if (!writeAll(
                            pipe,
                            response.data(),
                            static_cast<DWORD>(response.size()),
                            m_running
                        )) {
                            break;
                        }
                    }
                }
            }
        }

        m_connected.store(false);

        if (connected)
            DisconnectNamedPipe(pipe);

        CloseHandle(pipe);
    }

    m_connected.store(false);
}
