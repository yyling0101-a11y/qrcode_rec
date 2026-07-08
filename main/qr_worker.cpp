#include "qr_worker.hpp"

#include "qr_detector.hpp"

#include <chrono>
#include <cstdio>

namespace {
int64_t currentUnixMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
}

void runQrWorker(LatestFrameQueue& queue,
                 QrResultStore& store,
                 std::atomic<bool>& running) {
    QrDetector detector;
    if (!detector.valid()) {
        store.updateError(detector.lastError());
        std::fprintf(stderr, "[qr] detector init failed: %s\n", detector.lastError().c_str());
        return;
    }

    uint64_t scannedFrames = 0;
    uint64_t detectedFrames = 0;

    while (running.load()) {
        GrayFrame frame;
        if (!queue.waitPop(frame, running)) {
            continue;
        }

        const int64_t startMs = currentUnixMs();
        std::vector<QrCodeResult> codes = detector.detect(frame.gray.data(),
                                                          frame.width,
                                                          frame.height,
                                                          frame.stride);
        const int64_t endMs = currentUnixMs();
        ++scannedFrames;

        if (!codes.empty()) {
            ++detectedFrames;
            store.updateFound(frame, scannedFrames, detectedFrames, startMs, endMs, codes);
            std::printf("[qr] frame=%llu codes=%zu cost=%lldms text=%s\n",
                        static_cast<unsigned long long>(frame.frame_id),
                        codes.size(),
                        static_cast<long long>(endMs - startMs),
                        codes[0].text.c_str());
        } else {
            store.updateNoQr(frame, scannedFrames, startMs, endMs);
        }
    }
}
