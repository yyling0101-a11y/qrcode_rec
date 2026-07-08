#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct QrPoint {
    int x = 0;
    int y = 0;
};

struct QrCodeResult {
    std::string text;
    std::vector<uint8_t> payload;
    QrPoint corners[4];
};

class QrDetector {
public:
    QrDetector();
    ~QrDetector();

    QrDetector(const QrDetector&) = delete;
    QrDetector& operator=(const QrDetector&) = delete;

    bool valid() const;
    const std::string& lastError() const;

    std::vector<QrCodeResult> detect(const uint8_t* gray,
                                     int width,
                                     int height,
                                     int stride);

private:
    void* quirc_ = nullptr;
    std::string last_error_;
};
