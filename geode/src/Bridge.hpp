
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

    // Returns a pending preset request, or -1 if there isn't one.
    int takeRequestedPreset();

private:
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};

    // -1 means no pending request.
    std::atomic<int> m_requestedPreset{-1};

    std::thread m_worker;

    void workerLoop();
};
