# reCamera QR PyQt Viewer

Windows 端 PyQt 客户端：

- 左侧显示 reCamera 的 RTSP 实时画面
- 右侧显示最后一次获取到的二维码检测结果
- 后台线程轮询 `/api/qr/latest`
- 自动把二维码 bbox 缩放并绘制到 RTSP 画面上
- 支持 RTSP 断线重连

## 安装

```powershell
python -m venv .venv
.\.venv\Scripts\activate
pip install -r requirements.txt
```

## 运行

根据你的设备日志，建议先使用 live0：

```powershell
python recamera_qr_pyqt_viewer.py --ip 192.168.4.5 --rtsp-path live0
```

或者手动指定完整地址：

```powershell
python recamera_qr_pyqt_viewer.py ^
  --rtsp rtsp://192.168.4.5:8554/live0 ^
  --qr-url http://192.168.4.5:8080/api/qr/latest
```

## 常见问题

### RTSP 黑屏

你的设备日志里可能同时打印：

```text
rtsp://192.168.4.5:8554/live0
rtsp://192.168.4.5:8554/onvif
```

实际可用路径以 rtsp demo 初始化时打印的 session name 为准。你的日志里是：

```text
session name=live0
```

所以 Windows 端优先使用：

```text
rtsp://192.168.4.5:8554/live0
```

### 有二维码结果但画面不画框

确认 `/api/qr/latest` 返回中包含：

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

客户端会用 `frame_width/frame_height` 将检测坐标缩放到 RTSP 显示画面。

### OpenCV 拉 RTSP 延迟较大

本客户端已经设置了：

```python
cv2.CAP_PROP_BUFFERSIZE = 1
```

但 Windows 下具体是否生效取决于 OpenCV 后端。后续如果需要更低延迟，可以改成 PyAV、FFmpeg 子进程或 GStreamer。
