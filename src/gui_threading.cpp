#include "openterface/gui_threading.hpp"

namespace openterface {

    // SurfaceUpdateQueue implementation
    void SurfaceUpdateQueue::push(const SurfaceCommitRequest& request) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(request);
    }

    bool SurfaceUpdateQueue::pop(SurfaceCommitRequest& request) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) {
            return false;
        }
        request = queue.front();
        queue.pop();
        return true;
    }

    bool SurfaceUpdateQueue::empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }

    void SurfaceUpdateQueue::clear() {
        std::lock_guard<std::mutex> lock(mutex);
        while (!queue.empty()) {
            queue.pop();
        }
    }

    // ThreadManager implementation
    ThreadManager::~ThreadManager() {
        stopWaylandEventThread();
        stopRenderThread();
        stopInputThread();
    }

    void ThreadManager::startWaylandEventThread(std::function<void()> func) {
        if (wayland_thread_running.load()) return;
        
        wayland_thread_running = true;
        wayland_event_thread = std::thread(func);
    }

    void ThreadManager::stopWaylandEventThread() {
        if (!wayland_thread_running.load()) return;
        
        wayland_thread_running = false;
        
        if (wayland_event_thread.joinable()) {
            wayland_event_thread.join();
        }
    }

    void ThreadManager::startRenderThread(std::function<void()> func) {
        if (render_thread_running.load()) return;
        
        render_thread_running = true;
        render_thread = std::thread(func);
    }

    void ThreadManager::stopRenderThread() {
        if (!render_thread_running.load()) return;
        
        render_thread_running = false;
        render_cv.notify_all();
        
        if (render_thread.joinable()) {
            render_thread.join();
        }
    }

    void ThreadManager::startInputThread(std::function<void()> func) {
        if (input_thread_running.load()) return;
        
        input_thread_running = true;
        input_thread = std::thread(func);
    }

    void ThreadManager::stopInputThread() {
        if (!input_thread_running.load()) return;
        
        input_thread_running = false;
        input_cv.notify_all();
        
        if (input_thread.joinable()) {
            input_thread.join();
        }
    }

} // namespace openterface