
#pragma once

#include <atomic>
#include <thread>

class ColdplayBridge {
public:
    ColdplayBridge() = default;
    ~ColdplayBridge();

    ColdplayBridge(const ColdplayBridge&) = delete;
    ColdplayBridge& operator=(const ColdplayBridge&) = delete;

    void start();
    void stop();

    bool isConnected() const;

private:
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};

    std::thread m_worker;

    void workerLoop();
};
