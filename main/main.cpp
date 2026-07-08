#include "http_server.hpp"
#include "latest_frame_queue.hpp"
#include "qr_result_store.hpp"
#include "qr_worker.hpp"
#include "rtsp_demo.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ifaddrs.h>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>

extern "C" {
#include "cvi_sys.h"
#include "cvi_venc.h"
#include "video.h"
}

namespace {

constexpr video_ch_index_t kQrChannel     = VIDEO_CH1;
constexpr video_ch_index_t kRtspChannel   = VIDEO_CH2;

// 二维码检测通道。320x180 对小二维码太低，建议先用 640x360。
constexpr int kQrWidth                    = 640;
constexpr int kQrHeight                   = 360;
constexpr int kQrFps                      = 10;

// RTSP 预览通道。
constexpr int kRtspWidth                  = 1920;
constexpr int kRtspHeight                 = 1080;
constexpr int kRtspFps                    = 30;

std::atomic<bool> gVideoStarted(false);
std::atomic<bool> gRtspStarted(false);
std::atomic<bool> gRunning(true);
LatestFrameQueue* gQueueForSignal = nullptr;

std::string jsonMessage(bool ok, const std::string& message) {
    std::string body = "{\n  \"ok\": ";
    body += ok ? "true" : "false";
    body += ok ? ",\n  \"message\": \"" : ",\n  \"error\": \"";
    body += message;
    body += "\"\n}\n";
    return body;
}

std::int64_t currentUnixMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string getLocalIp() {
    const char* forced = std::getenv("QR_DEVICE_IP");
    if (forced != nullptr && forced[0] != '\0') {
        return forced;
    }

    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) {
        return "127.0.0.1";
    }

    const char* preferred[] = {"eth0", "wlan0", "usb0", "end0"};
    std::string fallback;

    for (ifaddrs* current = interfaces; current != nullptr; current = current->ifa_next) {
        if (current->ifa_addr == nullptr || current->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        const sockaddr_in* address = reinterpret_cast<const sockaddr_in*>(current->ifa_addr);
        char ip[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)) == nullptr) {
            continue;
        }
        if (std::strcmp(ip, "127.0.0.1") == 0) {
            continue;
        }

        if (fallback.empty()) {
            fallback = ip;
        }

        for (const char* name : preferred) {
            if (std::strcmp(current->ifa_name, name) == 0) {
                const std::string result = ip;
                freeifaddrs(interfaces);
                return result;
            }
        }
    }

    freeifaddrs(interfaces);
    return fallback.empty() ? "127.0.0.1" : fallback;
}

int setH264Gop(VENC_CHN channel, CVI_U32 newGop) {
    VENC_CHN_ATTR_S attributes;
    std::memset(&attributes, 0, sizeof(attributes));

    CVI_S32 result = CVI_VENC_GetChnAttr(channel, &attributes);
    if (result != CVI_SUCCESS) {
        return result;
    }

    switch (attributes.stRcAttr.enRcMode) {
        case VENC_RC_MODE_H264CBR:
            attributes.stRcAttr.stH264Cbr.u32Gop = newGop;
            break;
        case VENC_RC_MODE_H264VBR:
            attributes.stRcAttr.stH264Vbr.u32Gop = newGop;
            break;
        case VENC_RC_MODE_H264AVBR:
            attributes.stRcAttr.stH264AVbr.u32Gop = newGop;
            break;
        case VENC_RC_MODE_H264FIXQP:
            attributes.stRcAttr.stH264FixQp.u32Gop = newGop;
            break;
        default:
            std::fprintf(stderr, "[video] unsupported H264 RC mode=%d; GOP unchanged\n",
                         static_cast<int>(attributes.stRcAttr.enRcMode));
            return CVI_FAILURE;
    }

    result = CVI_VENC_SetChnAttr(channel, &attributes);
    if (result == CVI_SUCCESS) {
        CVI_VENC_RequestIDR(channel, CVI_TRUE);
    }
    return result;
}

int qrFrameCallback(void* data, void* args, void* userData) {
    (void)args;

    LatestFrameQueue* queue = static_cast<LatestFrameQueue*>(userData);
    VIDEO_FRAME_INFO_S* frameInfo = static_cast<VIDEO_FRAME_INFO_S*>(data);

    if (queue == nullptr || frameInfo == nullptr) {
        return CVI_FAILURE;
    }

    VIDEO_FRAME_S* frame = &frameInfo->stVFrame;
    if (frame->u64PhyAddr[0] == 0 || frame->u32Length[0] == 0) {
        return CVI_FAILURE;
    }

    CVI_U8* mapped = static_cast<CVI_U8*>(CVI_SYS_Mmap(frame->u64PhyAddr[0], frame->u32Length[0]));
    if (mapped == nullptr) {
        return CVI_FAILURE;
    }

    const int width = static_cast<int>(frame->u32Width);
    const int height = static_cast<int>(frame->u32Height);
    const int stride = frame->u32Stride[0] > 0
        ? static_cast<int>(frame->u32Stride[0])
        : width;

    static std::atomic<uint64_t> frameIdCounter{0};
    const uint64_t frameId = ++frameIdCounter;

    queue->pushLatest(mapped,
                      width,
                      height,
                      stride,
                      frameId,
                      frame->u64PTS,
                      currentUnixMs());

    CVI_SYS_Munmap(mapped, frame->u32Length[0]);
    return CVI_SUCCESS;
}

void stopVideoStack() {
    if (gVideoStarted.exchange(false)) {
        deinitVideo();
    }
    if (gRtspStarted.exchange(false)) {
        deinitRtsp();
    }
}

void signalHandler(int signalNumber) {
    std::fprintf(stderr, "\n[main] signal=%d, shutting down\n", signalNumber);
    gRunning.store(false);
    if (gQueueForSignal != nullptr) {
        gQueueForSignal->wakeup();
    }
    stopVideoStack();
    _exit(128 + signalNumber);
}

int startVideoStack(LatestFrameQueue& qrQueue) {
    if (geteuid() != 0) {
        std::fprintf(stderr, "[fatal] reCamera video SDK must run as root\n");
        return -EPERM;
    }

    int result = initVideo();
    if (result != 0) {
        std::fprintf(stderr, "[video] initVideo failed: %d\n", result);
        return result;
    }

    video_ch_param_t qrParam{};
    qrParam.format = VIDEO_FORMAT_NV21;
    qrParam.width = kQrWidth;
    qrParam.height = kQrHeight;
    qrParam.fps = kQrFps;

    result = setupVideo(kQrChannel, &qrParam);
    if (result != 0) {
        std::fprintf(stderr, "[video] setup QR channel failed: %d\n", result);
        deinitVideo();
        return result;
    }

    result = registerVideoFrameHandler(kQrChannel, 0, qrFrameCallback, &qrQueue);
    if (result != 0) {
        std::fprintf(stderr, "[video] register QR callback failed: %d\n", result);
        deinitVideo();
        return result;
    }

    video_ch_param_t rtspParam{};
    rtspParam.format = VIDEO_FORMAT_H264;
    rtspParam.width = kRtspWidth;
    rtspParam.height = kRtspHeight;
    rtspParam.fps = kRtspFps;

    result = setupVideo(kRtspChannel, &rtspParam);
    if (result != 0) {
        std::fprintf(stderr, "[video] setup H264 channel failed: %d\n", result);
        deinitVideo();
        return result;
    }

    result = registerVideoFrameHandler(kRtspChannel, 0, fpStreamingSendToRtsp, nullptr);
    if (result != 0) {
        std::fprintf(stderr, "[video] register RTSP callback failed: %d\n", result);
        deinitVideo();
        return result;
    }

    const std::uint8_t rtspMask = static_cast<std::uint8_t>(1U << static_cast<unsigned>(kRtspChannel));
    result = initRtsp(rtspMask);
    if (result != 0) {
        std::fprintf(stderr, "[rtsp] initRtsp failed: %d\n", result);
        deinitVideo();
        return result;
    }
    gRtspStarted.store(true);

    result = startVideo();
    if (result != 0) {
        std::fprintf(stderr, "[video] startVideo failed: %d\n", result);
        if (gRtspStarted.exchange(false)) {
            deinitRtsp();
        }
        deinitVideo();
        return result;
    }
    gVideoStarted.store(true);

    const CVI_S32 gopResult = setH264Gop(static_cast<VENC_CHN>(kRtspChannel), kRtspFps);
    if (gopResult != CVI_SUCCESS) {
        std::fprintf(stderr, "[video] warning: failed to set GOP: %d (0x%x)\n",
                     gopResult,
                     static_cast<unsigned int>(gopResult));
    }

    return 0;
}

}  // namespace

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    int httpPort = 8080;
    const char* portEnvironment = std::getenv("QR_HTTP_PORT");
    if (portEnvironment != nullptr && portEnvironment[0] != '\0') {
        httpPort = std::atoi(portEnvironment);
        if (httpPort <= 0 || httpPort > 65535) {
            httpPort = 8080;
        }
    }

    LatestFrameQueue qrQueue;
    QrResultStore qrStore;
    gQueueForSignal = &qrQueue;

    std::thread qrThread([&]() {
        runQrWorker(qrQueue, qrStore, gRunning);
    });

    const int videoResult = startVideoStack(qrQueue);
    if (videoResult != 0) {
        gRunning.store(false);
        qrQueue.wakeup();
        if (qrThread.joinable()) {
            qrThread.join();
        }
        return EXIT_FAILURE;
    }

    const std::string ip = getLocalIp();
    std::printf("\nreCamera QR scanner is running\n");
    std::printf("RTSP      : rtsp://%s:8554/onvif\n", ip.c_str());
    std::printf("QR latest : http://%s:%d/api/qr/latest\n", ip.c_str(), httpPort);
    std::printf("Health    : http://%s:%d/api/health\n\n", ip.c_str(), httpPort);
    std::fflush(stdout);

    HttpServer server(httpPort);
    server.setHandler([&](const HttpRequest& request) {
        HttpResponse response;
        response.contentType = "application/json; charset=utf-8";

        if (request.method == "GET" && request.path == "/") {
            response.contentType = "text/plain; charset=utf-8";
            response.body =
                "reCamera QR Scanner\n"
                "GET /api/health\n"
                "GET /api/qr/latest\n";
            return response;
        }

        if (request.method == "GET" && request.path == "/api/health") {
            response.body =
                "{\n"
                "  \"ok\": true,\n"
                "  \"device_ip\": \"" + ip + "\",\n"
                "  \"rtsp_url\": \"rtsp://" + ip + ":8554/onvif\",\n"
                "  \"qr_latest_url\": \"http://" + ip + ":" + std::to_string(httpPort) + "/api/qr/latest\",\n"
                "  \"qr_width\": " + std::to_string(kQrWidth) + ",\n"
                "  \"qr_height\": " + std::to_string(kQrHeight) + ",\n"
                "  \"qr_fps\": " + std::to_string(kQrFps) + ",\n"
                "  \"rtsp_width\": " + std::to_string(kRtspWidth) + ",\n"
                "  \"rtsp_height\": " + std::to_string(kRtspHeight) + ",\n"
                "  \"rtsp_fps\": " + std::to_string(kRtspFps) + "\n"
                "}\n";
            return response;
        }

        if (request.method == "GET" && request.path == "/api/qr/latest") {
            response.body = qrStore.latestJson(qrQueue.pushedCount(), qrQueue.droppedCount());
            return response;
        }

        response.status = 404;
        response.body = jsonMessage(false, "endpoint not found");
        return response;
    });

    server.start();

    gRunning.store(false);
    qrQueue.wakeup();
    if (qrThread.joinable()) {
        qrThread.join();
    }
    stopVideoStack();
    return EXIT_SUCCESS;
}
