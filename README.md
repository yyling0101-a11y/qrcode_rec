# Real-Time QR Code Recognition on reCamera

This repository demonstrates how to build a real-time QR code recognition application on **reCamera**.

The application runs directly on reCamera. It captures camera frames, streams live video through **RTSP**, detects QR codes asynchronously with the **quirc** library, and exposes the latest QR code result through an HTTP API. A PC-side PyQt viewer can display the RTSP stream on the left and the latest QR code result on the right.

![reCamera QR Code Demo](https://files.seeedstudio.com/wiki/reCamera/Applications/Qrcode_rec/1.gif)

## Features

- Real-time camera capture on reCamera
- H.264 RTSP video streaming
- Asynchronous QR code detection with a dedicated worker thread
- Latest-frame queue with length 1 to avoid detection backlog
- QR code decoding with `quirc`
- HTTP API for querying the latest QR code result
- PyQt-based PC viewer for video and detection result display

## How It Works

### Video Stream + QR Code Result Separation Mode

This example separates live video transmission from QR code detection result retrieval.

The reCamera device captures camera frames and configures two video channels:

- One channel outputs H.264 video for RTSP streaming.
- One channel outputs low-resolution NV21 frames for QR code detection.

The QR code detection thread is decoupled from the video capture callback. The callback only copies the latest grayscale frame into a queue with a length of 1. If the detection thread is still processing an older frame, the new frame overwrites the old one. This ensures that the detector always works on the newest frame and prevents latency from accumulating.

The PC-side client pulls the live video stream through RTSP and periodically queries the HTTP API for the latest QR code result.

### Data Links

| Data Type | Protocol | Address | Description |
| --- | --- | --- | --- |
| Live video stream | RTSP | `rtsp://<device-ip>:8554/live0` | Real-time H.264 video stream |
| QR code result | HTTP API | `http://<device-ip>:8080/api/qr/latest` | Latest QR code detection result |
| Health check | HTTP API | `http://<device-ip>:8080/api/health` | Runtime status check |

### Runtime Flow

1. **Camera capture**  
   reCamera captures video frames through the SG2002 video pipeline.

2. **RTSP video streaming**  
   The main video channel is encoded as H.264 and streamed through RTSP.

3. **QR detection frame acquisition**  
   A secondary NV21 channel provides low-resolution frames for QR code detection. The program uses the Y plane as grayscale input.

4. **Latest-frame queue**  
   The capture callback pushes the newest grayscale frame into a one-frame queue. Old frames are discarded when the queue is full.

5. **Asynchronous QR detection**  
   A QR worker thread reads the latest frame and uses `quirc` to detect and decode QR codes.

6. **Result cache**  
   The latest detection result is stored in memory, including QR text, frame ID, PTS, capture timestamp, detection timestamp, detection cost, and bounding box coordinates.

7. **HTTP result query**  
   Other devices on the same network can query `GET /api/qr/latest` to obtain the latest result.

8. **PC display**  
   The PyQt viewer displays the RTSP video on the left and the latest QR result on the right. If bounding box coordinates are available, the viewer overlays the QR code box on the video.

## Repository Structure

```text
qrcode_rec
├── CMakeLists.txt
├── main
│   ├── CMakeLists.txt
│   ├── frame_sei.cpp
│   ├── frame_sei.hpp
│   ├── frame_sync.cpp
│   ├── frame_sync.hpp
│   ├── http_server.cpp
│   ├── http_server.hpp
│   ├── latest_frame_queue.hpp
│   ├── main.cpp
│   ├── qr_detector.cpp
│   ├── qr_detector.hpp
│   ├── qr_result_store.cpp
│   ├── qr_result_store.hpp
│   ├── qr_worker.cpp
│   ├── qr_worker.hpp
│   ├── rtsp_demo.cpp
│   └── rtsp_demo.h
├── third_party
│   └── quirc
│       └── lib
│           ├── decode.c
│           ├── identify.c
│           ├── quirc.c
│           ├── quirc.h
│           ├── quirc_internal.h
│           └── version_db.c
└── recamera_qr_pyqt_client
    ├── README.md
    ├── recamera_qr_pyqt_viewer.py
    └── requirements.txt
```

## Build on PC

### 1. Prepare the SG2002 SDK

Before building this solution, configure the reCamera SG2002 build environment.

Download the corresponding SDK or example package:

```text
https://codeload.github.com/Seeed-Studio/sscma-example-sg200x/tar.gz/refs/tags/0.2.4
```

Set the environment variables according to your local SDK path:

```bash
export SG200X_SDK_PATH=<PATH_TO_RECAMERA_OS>/output/sg2002_recamera_emmc
export PATH=<PATH_TO_RECAMERA_OS>/host-tools/gcc/riscv64-linux-musl-x86_64/bin:$PATH
```

> Replace `<PATH_TO_RECAMERA_OS>` with your actual reCamera OS or SDK directory.

### 2. Clone This Repository

```bash
git clone https://github.com/yyling0101-a11y/qrcode_rec.git
cd qrcode_rec
```

### 3. Build

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

After a successful build, the executable will be generated as:

```text
build/qrcode_rec
```

## Deploy to reCamera

Copy the executable to your reCamera:

```bash
scp build/qrcode_rec recamera@<device-ip>:/home/recamera/
```

Example:

```bash
scp build/qrcode_rec recamera@192.168.4.5:/home/recamera/
```

## Run on reCamera

### 1. Stop Default Services

Before running the program, stop the default services that may occupy camera resources:

```bash
sudo /etc/init.d/S03node-red stop
sudo /etc/init.d/S91sscma-node stop
sudo /etc/init.d/S93sscma-supervisor stop
```

### 2. Start the QR Code Application

```bash
cd /home/recamera
chmod +x qrcode_rec
sudo ./qrcode_rec
```

A successful startup log looks similar to this:

```text
[recamera@reCamera]~$ sudo ./qrcode_rec
prio:0
rtsp://192.168.4.5:8554/live0
[rtsp] session name=live0 channel=2 codec=1 result=0 session=0x3fe0c53210

reCamera QR scanner is running
RTSP      : rtsp://192.168.4.5:8554/live0
QR latest : http://192.168.4.5:8080/api/qr/latest
Health    : http://192.168.4.5:8080/api/health

[http] listening on 0.0.0.0:8080
```

> If your local log prints both `/live0` and `/onvif`, use the RTSP session name printed by the RTSP module. In this example, the valid path is usually `/live0`.

## Test the HTTP API

From a PC on the same network, run:

```bash
curl http://<device-ip>:8080/api/health
curl http://<device-ip>:8080/api/qr/latest
```

Example:

```bash
curl http://192.168.4.5:8080/api/qr/latest
```

A typical QR result response is:

```json
{
  "ok": true,
  "type": "qr_latest",
  "queue_pushed": 120,
  "queue_dropped": 15,
  "scanned_frames": 80,
  "detected_frames": 3,
  "frame_id": 120,
  "pts": 123456789,
  "capture_unix_ms": 1783470000123,
  "detect_start_unix_ms": 1783470000200,
  "detect_end_unix_ms": 1783470000238,
  "detect_cost_ms": 38,
  "frame_width": 640,
  "frame_height": 360,
  "qr_found": true,
  "codes": [
    {
      "text": "https://www.seeedstudio.com",
      "bbox": [
        {"x": 120, "y": 80},
        {"x": 220, "y": 82},
        {"x": 218, "y": 180},
        {"x": 118, "y": 178}
      ]
    }
  ]
}
```

## Run the PyQt Viewer on PC

The PC viewer displays:

- RTSP video on the left
- Latest QR code result on the right
- Raw JSON returned by the HTTP API
- Optional QR bounding box overlay on the video

### 1. Install Dependencies

```bash
cd recamera_qr_pyqt_client
python -m venv .venv
```

On Windows PowerShell:

```powershell
.\.venv\Scripts\activate
pip install -r requirements.txt
```

On Linux:

```bash
source .venv/bin/activate
pip install -r requirements.txt
```

### 2. Run the Viewer

Windows PowerShell:

```powershell
python recamera_qr_pyqt_viewer.py `
  --rtsp rtsp://192.168.4.5:8554/live0 `
  --qr-url http://192.168.4.5:8080/api/qr/latest
```

Linux:

```bash
python3 recamera_qr_pyqt_viewer.py \
  --rtsp rtsp://192.168.4.5:8554/live0 \
  --qr-url http://192.168.4.5:8080/api/qr/latest
```

### Viewer Parameters

| Parameter | Description | Default |
| --- | --- | --- |
| `--ip` | reCamera IP address | `192.168.4.5` |
| `--rtsp` | Full RTSP URL | Empty. If not set, generated from `--ip`, `--rtsp-port`, and `--rtsp-path`. |
| `--qr-url` | Full QR result API URL | Empty. If not set, generated from `--ip` and `--http-port`. |
| `--rtsp-port` | RTSP port | `8554` |
| `--rtsp-path` | RTSP stream path | `live0` |
| `--http-port` | HTTP API port | `8080` |
| `--qr-interval` | QR API polling interval in seconds | `0.2` |

![PC Viewer](https://files.seeedstudio.com/wiki/reCamera/Applications/Qrcode_rec/1.png)

## Troubleshooting

### RTSP Stream Cannot Be Opened

Check whether the RTSP path is correct.

The device log usually prints the valid session name:

```text
[rtsp] session name=live0
```

In that case, use:

```text
rtsp://<device-ip>:8554/live0
```

You can also test the stream with `ffplay`:

```bash
ffplay rtsp://<device-ip>:8554/live0
```

### QR API Cannot Be Accessed

Check whether the HTTP server is running:

```bash
curl http://<device-ip>:8080/api/health
```

Also make sure your PC and reCamera are on the same network segment.

### QR Result Exists But the Viewer Does Not Draw the Box

Make sure the API response contains these fields:

```json
{
  "frame_width": 640,
  "frame_height": 360,
  "codes": [
    {
      "text": "...",
      "bbox": [
        {"x": 1, "y": 2}
      ]
    }
  ]
}
```

The viewer uses `frame_width` and `frame_height` to scale the QR bounding box onto the RTSP video frame.

### Camera Cannot Be Opened

Stop the default camera-related services before starting the program:

```bash
sudo /etc/init.d/S03node-red stop
sudo /etc/init.d/S91sscma-node stop
sudo /etc/init.d/S93sscma-supervisor stop
```

## Notes

- The QR detection channel uses low-resolution NV21 frames to reduce CPU overhead.
- The RTSP stream is independent of the QR detection thread.
- The latest-frame queue keeps only one frame. This prevents detection latency from increasing when QR decoding is slower than camera capture.
- The HTTP API returns only the latest detection result, not historical results.
- For higher QR recognition accuracy, increase the detection channel resolution in the C++ source code, such as from `640x360` to `1280x720`.

## License

This example includes the `quirc` QR decoder. Please check `third_party/quirc/LICENSE` for quirc's license information.
