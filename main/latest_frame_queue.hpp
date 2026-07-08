#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

struct GrayFrame {
    uint64_t frame_id = 0;
    uint64_t pts = 0;
    int64_t capture_unix_ms = 0;

    int width = 0;
    int height = 0;
    int stride = 0;  // compact stride after copy, normally equals width

    std::vector<uint8_t> gray;
};

class LatestFrameQueue {
public:
    void pushLatest(const uint8_t* y,
                    int width,
                    int height,
                    int srcStride,
                    uint64_t frameId,
                    uint64_t pts,
                    int64_t captureUnixMs) {
        if (y == nullptr || width <= 0 || height <= 0 || srcStride < width) {
            return;
        }

        GrayFrame frame;
        frame.frame_id = frameId;
        frame.pts = pts;
        frame.capture_unix_ms = captureUnixMs;
        frame.width = width;
        frame.height = height;
        frame.stride = width;
        frame.gray.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

        for (int row = 0; row < height; ++row) {
            std::memcpy(frame.gray.data() + static_cast<size_t>(row) * width,
                        y + static_cast<size_t>(row) * srcStride,
                        static_cast<size_t>(width));
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (has_frame_) {
                ++dropped_count_;
            }
            latest_ = std::move(frame);
            has_frame_ = true;
            ++pushed_count_;
        }
        cond_.notify_one();
    }

    bool waitPop(GrayFrame& out, const std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [&] {
            return has_frame_ || !running.load();
        });

        if (!has_frame_) {
            return false;
        }

        out = std::move(latest_);
        has_frame_ = false;
        return true;
    }

    void wakeup() {
        cond_.notify_all();
    }

    uint64_t pushedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pushed_count_;
    }

    uint64_t droppedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_count_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    GrayFrame latest_;
    bool has_frame_ = false;
    uint64_t pushed_count_ = 0;
    uint64_t dropped_count_ = 0;
};
