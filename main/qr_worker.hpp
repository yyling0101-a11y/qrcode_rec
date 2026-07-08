#pragma once

#include "latest_frame_queue.hpp"
#include "qr_result_store.hpp"

#include <atomic>

void runQrWorker(LatestFrameQueue& queue,
                 QrResultStore& store,
                 std::atomic<bool>& running);
