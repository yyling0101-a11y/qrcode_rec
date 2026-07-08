#include "qr_result_store.hpp"

#include <sstream>

void QrResultStore::updateNoQr(const GrayFrame& frame,
                               uint64_t scannedFrames,
                               int64_t detectStartUnixMs,
                               int64_t detectEndUnixMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.ok = true;
    latest_.error.clear();
    latest_.scanned_frames = scannedFrames;
    latest_.frame_id = frame.frame_id;
    latest_.pts = frame.pts;
    latest_.capture_unix_ms = frame.capture_unix_ms;
    latest_.detect_start_unix_ms = detectStartUnixMs;
    latest_.detect_end_unix_ms = detectEndUnixMs;
    latest_.detect_cost_ms = static_cast<int>(detectEndUnixMs - detectStartUnixMs);
    latest_.frame_width = frame.width;
    latest_.frame_height = frame.height;
    latest_.qr_found = false;
    latest_.codes.clear();
}

void QrResultStore::updateFound(const GrayFrame& frame,
                                uint64_t scannedFrames,
                                uint64_t detectedFrames,
                                int64_t detectStartUnixMs,
                                int64_t detectEndUnixMs,
                                const std::vector<QrCodeResult>& codes) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.ok = true;
    latest_.error.clear();
    latest_.scanned_frames = scannedFrames;
    latest_.detected_frames = detectedFrames;
    latest_.frame_id = frame.frame_id;
    latest_.pts = frame.pts;
    latest_.capture_unix_ms = frame.capture_unix_ms;
    latest_.detect_start_unix_ms = detectStartUnixMs;
    latest_.detect_end_unix_ms = detectEndUnixMs;
    latest_.detect_cost_ms = static_cast<int>(detectEndUnixMs - detectStartUnixMs);
    latest_.frame_width = frame.width;
    latest_.frame_height = frame.height;
    latest_.qr_found = true;
    latest_.codes = codes;
}

void QrResultStore::updateError(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.ok = false;
    latest_.error = error;
}

std::string QrResultStore::escapeJson(const std::string& input) {
    std::ostringstream out;
    for (unsigned char c : input) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out << "\\u00" << hex[(c >> 4) & 0x0F] << hex[c & 0x0F];
                } else {
                    out << static_cast<char>(c);
                }
                break;
        }
    }
    return out.str();
}

std::string QrResultStore::latestJson(uint64_t queuePushed, uint64_t queueDropped) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;

    out << "{\n";
    out << "  \"ok\": " << (latest_.ok ? "true" : "false") << ",\n";
    if (!latest_.ok) {
        out << "  \"error\": \"" << escapeJson(latest_.error) << "\",\n";
    }
    out << "  \"type\": \"qr_latest\",\n";
    out << "  \"queue_pushed\": " << queuePushed << ",\n";
    out << "  \"queue_dropped\": " << queueDropped << ",\n";
    out << "  \"scanned_frames\": " << latest_.scanned_frames << ",\n";
    out << "  \"detected_frames\": " << latest_.detected_frames << ",\n";
    out << "  \"frame_id\": " << latest_.frame_id << ",\n";
    out << "  \"pts\": " << latest_.pts << ",\n";
    out << "  \"capture_unix_ms\": " << latest_.capture_unix_ms << ",\n";
    out << "  \"detect_start_unix_ms\": " << latest_.detect_start_unix_ms << ",\n";
    out << "  \"detect_end_unix_ms\": " << latest_.detect_end_unix_ms << ",\n";
    out << "  \"detect_cost_ms\": " << latest_.detect_cost_ms << ",\n";
    out << "  \"frame_width\": " << latest_.frame_width << ",\n";
    out << "  \"frame_height\": " << latest_.frame_height << ",\n";
    out << "  \"qr_found\": " << (latest_.qr_found ? "true" : "false") << ",\n";
    out << "  \"codes\": [";

    for (size_t i = 0; i < latest_.codes.size(); ++i) {
        const QrCodeResult& code = latest_.codes[i];
        if (i > 0) {
            out << ",";
        }
        out << "\n    {\n";
        out << "      \"text\": \"" << escapeJson(code.text) << "\",\n";
        out << "      \"bbox\": [";
        for (int p = 0; p < 4; ++p) {
            if (p > 0) {
                out << ", ";
            }
            out << "{\"x\": " << code.corners[p].x << ", \"y\": " << code.corners[p].y << "}";
        }
        out << "]\n";
        out << "    }";
    }

    if (!latest_.codes.empty()) {
        out << "\n  ";
    }
    out << "]\n";
    out << "}\n";
    return out.str();
}
