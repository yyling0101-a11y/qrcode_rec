#include "qr_detector.hpp"

#include <algorithm>
#include <cstring>

extern "C" {
#include "quirc.h"
}

QrDetector::QrDetector() {
    quirc_ = quirc_new();
    if (quirc_ == nullptr) {
        last_error_ = "quirc_new failed";
    }
}

QrDetector::~QrDetector() {
    if (quirc_ != nullptr) {
        quirc_destroy(static_cast<struct quirc*>(quirc_));
        quirc_ = nullptr;
    }
}

bool QrDetector::valid() const {
    return quirc_ != nullptr;
}

const std::string& QrDetector::lastError() const {
    return last_error_;
}

std::vector<QrCodeResult> QrDetector::detect(const uint8_t* gray,
                                             int width,
                                             int height,
                                             int stride) {
    std::vector<QrCodeResult> results;

    if (quirc_ == nullptr) {
        last_error_ = "quirc instance is null";
        return results;
    }
    if (gray == nullptr || width <= 0 || height <= 0 || stride < width) {
        last_error_ = "invalid gray frame";
        return results;
    }

    struct quirc* qr = static_cast<struct quirc*>(quirc_);

    if (quirc_resize(qr, width, height) < 0) {
        last_error_ = "quirc_resize failed";
        return results;
    }

    int imageWidth = 0;
    int imageHeight = 0;
    uint8_t* image = quirc_begin(qr, &imageWidth, &imageHeight);
    if (image == nullptr || imageWidth != width || imageHeight != height) {
        last_error_ = "quirc_begin failed";
        return results;
    }

    for (int row = 0; row < height; ++row) {
        std::memcpy(image + static_cast<size_t>(row) * width,
                    gray + static_cast<size_t>(row) * stride,
                    static_cast<size_t>(width));
    }

    quirc_end(qr);

    const int count = quirc_count(qr);
    results.reserve(static_cast<size_t>(std::max(count, 0)));

    for (int i = 0; i < count; ++i) {
        struct quirc_code code;
        struct quirc_data data;
        std::memset(&code, 0, sizeof(code));
        std::memset(&data, 0, sizeof(data));

        quirc_extract(qr, i, &code);
        const quirc_decode_error_t err = quirc_decode(&code, &data);
        if (err != QUIRC_SUCCESS) {
            last_error_ = quirc_strerror(err);
            continue;
        }

        QrCodeResult result;
        result.payload.assign(data.payload, data.payload + data.payload_len);
        result.text.assign(reinterpret_cast<const char*>(data.payload),
                           reinterpret_cast<const char*>(data.payload + data.payload_len));

        for (int p = 0; p < 4; ++p) {
            result.corners[p].x = code.corners[p].x;
            result.corners[p].y = code.corners[p].y;
        }
        results.push_back(std::move(result));
    }

    if (!results.empty()) {
        last_error_.clear();
    }

    return results;
}
