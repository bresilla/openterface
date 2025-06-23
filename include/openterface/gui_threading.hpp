#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace openterface {

    // Surface commit request for thread-safe Wayland operations
    struct SurfaceCommitRequest {
        enum Type {
            ATTACH_BUFFER,
            DAMAGE,
            COMMIT
        };
        
        Type type;
        int x = 0, y = 0, width = 0, height = 0;
        bool pending = true;
    };

    // Thread-safe message queue for surface updates
    class SurfaceUpdateQueue {
    public:
        void push(const SurfaceCommitRequest& request);
        bool pop(SurfaceCommitRequest& request);
        bool empty() const;
        void clear();

    private:
        mutable std::mutex mutex;
        std::queue<SurfaceCommitRequest> queue;
    };

    // Input event for thread processing
    struct InputEvent {
        enum Type { 
            MOUSE_MOVE, 
            MOUSE_BUTTON, 
            KEY_PRESS, 
            KEY_RELEASE 
        };
        
        Type type;
        int x = 0, y = 0;
        int button = 0;
        int key = 0;
        int modifiers = 0;
        bool pressed = false;
    };

    // Thread management helpers
    class ThreadManager {
    public:
        ThreadManager() = default;
        ~ThreadManager();

        // Start/stop threads
        void startWaylandEventThread(std::function<void()> func);
        void stopWaylandEventThread();
        
        void startRenderThread(std::function<void()> func);
        void stopRenderThread();
        
        void startInputThread(std::function<void()> func);
        void stopInputThread();

        // Thread state
        bool isWaylandThreadRunning() const { return wayland_thread_running.load(); }
        bool isRenderThreadRunning() const { return render_thread_running.load(); }
        bool isInputThreadRunning() const { return input_thread_running.load(); }

        // Synchronization
        void notifyRender() { render_cv.notify_one(); }
        void notifyInput() { input_cv.notify_one(); }

        // Thread-safe flags
        std::atomic<bool> wayland_thread_running{false};
        std::atomic<bool> render_thread_running{false};
        std::atomic<bool> input_thread_running{false};
        std::atomic<bool> frame_ready_for_render{false};
        std::atomic<bool> buffer_swap_ready{false};

        // Synchronization primitives
        std::mutex render_mutex;
        std::condition_variable render_cv;
        std::mutex input_mutex;
        std::condition_variable input_cv;

    private:
        std::thread wayland_event_thread;
        std::thread render_thread;
        std::thread input_thread;
    };

} // namespace openterface