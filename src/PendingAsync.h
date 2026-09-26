#pragma once
#include <atomic>
#include <mutex>
#include <winrt/Windows.Foundation.h>

// Tracks the WinRT async operation a worker thread is currently blocked on, so another thread
// can cancel it instead of waiting for a Bluetooth timeout during shutdown.
class PendingAsync {
public:
    template <typename Op>
    auto Await(Op const& op, std::atomic<bool> const& running) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_op = op;
        }
        if (!running) op.Cancel();
        struct Clear {
            PendingAsync* self;
            ~Clear() { std::lock_guard<std::mutex> lock(self->m_mutex); self->m_op = nullptr; }
        } clear{ this };
        return op.get();
    }

    void Cancel() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_op) {
            try { m_op.Cancel(); } catch (...) {}
        }
    }

private:
    std::mutex m_mutex;
    winrt::Windows::Foundation::IAsyncInfo m_op{ nullptr };
};
