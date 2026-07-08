#pragma once

#include "latest_frame_queue.hpp"
#include "qr_detector.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct QrDetectionSnapshot {
    bool ok = true;
    std::string error;

    uint64_t scanned_frames = 0;
    uint64_t detected_frames = 0;

    uint64_t frame_id = 0;
    uint64_t pts = 0;
    int64_t capture_unix_ms = 0;
    int64_t detect_start_unix_ms = 0;
    int64_t detect_end_unix_ms = 0;
    int detect_cost_ms = 0;

    int frame_width = 0;
    int frame_height = 0;

    bool qr_found = false;
    std::vector<QrCodeResult> codes;
};

class QrResultStore {
public:
    void updateNoQr(const GrayFrame& frame,
                    uint64_t scannedFrames,
                    int64_t detectStartUnixMs,
                    int64_t detectEndUnixMs);

    void updateFound(const GrayFrame& frame,
                     uint64_t scannedFrames,
                     uint64_t detectedFrames,
                     int64_t detectStartUnixMs,
                     int64_t detectEndUnixMs,
                     const std::vector<QrCodeResult>& codes);

    void updateError(const std::string& error);

    std::string latestJson(uint64_t queuePushed, uint64_t queueDropped) const;

private:
    static std::string escapeJson(const std::string& input);
    mutable std::mutex mutex_;
    QrDetectionSnapshot latest_;
};
