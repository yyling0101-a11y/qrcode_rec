#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import json
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any, Dict, List, Optional, Tuple

import cv2
import requests
from PyQt5.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt5.QtGui import QImage, QPixmap, QFont
from PyQt5.QtWidgets import (
    QApplication,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def unix_ms_to_text(value: Any) -> str:
    try:
        ms = int(value)
        if ms <= 0:
            return "-"
        return datetime.fromtimestamp(ms / 1000.0).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
    except Exception:
        return "-"


@dataclass
class SharedState:
    lock: threading.Lock = field(default_factory=threading.Lock)
    latest_frame_bgr: Optional[Any] = None
    video_status: str = "RTSP 未连接"
    qr_status: str = "QR API 未连接"
    latest_qr: Optional[Dict[str, Any]] = None
    latest_qr_fetch_time: str = "-"
    latest_qr_error: str = "-"
    running: bool = True


class RtspWorker(threading.Thread):
    def __init__(self, shared: SharedState, rtsp_url: str, reconnect_delay: float = 1.0):
        super().__init__(daemon=True)
        self.shared = shared
        self.rtsp_url = rtsp_url
        self.reconnect_delay = reconnect_delay

    def _set_status(self, status: str) -> None:
        with self.shared.lock:
            self.shared.video_status = status

    def run(self) -> None:
        while self.shared.running:
            self._set_status(f"正在连接 RTSP：{self.rtsp_url}")
            cap = cv2.VideoCapture(self.rtsp_url, cv2.CAP_FFMPEG)

            try:
                cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
            except Exception:
                pass

            if not cap.isOpened():
                self._set_status("RTSP 打开失败，准备重连")
                cap.release()
                time.sleep(self.reconnect_delay)
                continue

            self._set_status("RTSP 已连接")

            consecutive_failures = 0
            while self.shared.running:
                ok, frame = cap.read()
                if not ok or frame is None:
                    consecutive_failures += 1
                    if consecutive_failures >= 10:
                        self._set_status("RTSP 读取失败，准备重连")
                        break
                    time.sleep(0.03)
                    continue

                consecutive_failures = 0
                with self.shared.lock:
                    self.shared.latest_frame_bgr = frame
                    self.shared.video_status = "RTSP 正常"

            cap.release()
            time.sleep(self.reconnect_delay)


class QrPollWorker(threading.Thread):
    def __init__(self, shared: SharedState, qr_url: str, interval_sec: float = 0.2, timeout_sec: float = 0.8):
        super().__init__(daemon=True)
        self.shared = shared
        self.qr_url = qr_url
        self.interval_sec = interval_sec
        self.timeout_sec = timeout_sec

    def run(self) -> None:
        session = requests.Session()

        while self.shared.running:
            try:
                resp = session.get(self.qr_url, timeout=self.timeout_sec)
                resp.raise_for_status()
                data = resp.json()

                with self.shared.lock:
                    self.shared.latest_qr = data
                    self.shared.latest_qr_fetch_time = now_text()
                    self.shared.latest_qr_error = "-"
                    self.shared.qr_status = "QR API 正常"

            except Exception as exc:
                with self.shared.lock:
                    self.shared.latest_qr_error = str(exc)
                    self.shared.qr_status = "QR API 请求失败"

            time.sleep(self.interval_sec)


def parse_bbox(code: Dict[str, Any]) -> List[Tuple[float, float]]:
    bbox = code.get("bbox") or code.get("corners") or []
    points: List[Tuple[float, float]] = []

    for item in bbox:
        if isinstance(item, dict):
            try:
                points.append((float(item.get("x", 0)), float(item.get("y", 0))))
            except Exception:
                pass
        elif isinstance(item, (list, tuple)) and len(item) >= 2:
            try:
                points.append((float(item[0]), float(item[1])))
            except Exception:
                pass

    return points


def draw_qr_overlay(frame_bgr, qr_data: Optional[Dict[str, Any]]):
    if frame_bgr is None or not qr_data:
        return frame_bgr

    frame_h, frame_w = frame_bgr.shape[:2]

    source_w = int(qr_data.get("frame_width") or qr_data.get("analysis_width") or frame_w)
    source_h = int(qr_data.get("frame_height") or qr_data.get("analysis_height") or frame_h)

    if source_w <= 0:
        source_w = frame_w
    if source_h <= 0:
        source_h = frame_h

    sx = frame_w / float(source_w)
    sy = frame_h / float(source_h)

    codes = qr_data.get("codes") or []
    if not isinstance(codes, list):
        codes = []

    for idx, code in enumerate(codes):
        if not isinstance(code, dict):
            continue

        points = parse_bbox(code)
        if len(points) >= 4:
            pts = []
            for x, y in points[:4]:
                pts.append((int(round(x * sx)), int(round(y * sy))))

            for i in range(4):
                cv2.line(frame_bgr, pts[i], pts[(i + 1) % 4], (0, 255, 0), 2)

            label_x = max(0, min(frame_w - 1, pts[0][0]))
            label_y = max(20, min(frame_h - 1, pts[0][1] - 8))
        else:
            label_x, label_y = 20, 40 + idx * 30

        text = str(code.get("text") or code.get("data") or "")
        if text:
            display = text
            if len(display) > 80:
                display = display[:77] + "..."
            cv2.putText(
                frame_bgr,
                display,
                (label_x, label_y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 255, 0),
                2,
                cv2.LINE_AA,
            )

    return frame_bgr


def bgr_to_qpixmap(frame_bgr) -> QPixmap:
    frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
    h, w, ch = frame_rgb.shape
    bytes_per_line = ch * w
    image = QImage(frame_rgb.data, w, h, bytes_per_line, QImage.Format_RGB888)
    return QPixmap.fromImage(image.copy())


class MainWindow(QMainWindow):
    def __init__(self, shared: SharedState, rtsp_url: str, qr_url: str):
        super().__init__()
        self.shared = shared
        self.rtsp_url = rtsp_url
        self.qr_url = qr_url
        self.last_json_text = ""

        self.setWindowTitle("reCamera QR Viewer")
        self.resize(1280, 720)

        root = QWidget()
        self.setCentralWidget(root)

        main_layout = QHBoxLayout(root)
        main_layout.setContentsMargins(10, 10, 10, 10)
        main_layout.setSpacing(10)

        self.video_label = QLabel("等待 RTSP 画面...")
        self.video_label.setAlignment(Qt.AlignCenter)
        self.video_label.setMinimumSize(800, 450)
        self.video_label.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.video_label.setStyleSheet("background:#111; color:#ddd; border:1px solid #333;")

        main_layout.addWidget(self.video_label, 3)

        right_panel = QFrame()
        right_panel.setFrameShape(QFrame.StyledPanel)
        right_panel.setMinimumWidth(380)
        right_panel.setMaximumWidth(520)
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(12, 12, 12, 12)
        right_layout.setSpacing(8)

        title = QLabel("二维码检测结果")
        title_font = QFont()
        title_font.setPointSize(14)
        title_font.setBold(True)
        title.setFont(title_font)
        right_layout.addWidget(title)

        self.status_label = QLabel("-")
        self.status_label.setWordWrap(True)
        right_layout.addWidget(self.status_label)

        grid = QGridLayout()
        grid.setHorizontalSpacing(8)
        grid.setVerticalSpacing(6)

        self.value_labels: Dict[str, QLabel] = {}

        rows = [
            ("rtsp", "RTSP 状态"),
            ("qr_api", "QR API 状态"),
            ("fetch_time", "接口更新时间"),
            ("qr_found", "是否检测到"),
            ("qr_text", "二维码内容"),
            ("frame_id", "frame_id"),
            ("pts", "PTS"),
            ("capture_time", "采集时间"),
            ("detect_end_time", "检测完成时间"),
            ("detect_cost", "检测耗时"),
            ("scanned_frames", "扫描帧数"),
            ("detected_frames", "命中帧数"),
            ("queue", "队列统计"),
            ("error", "错误"),
        ]

        for row, (key, name) in enumerate(rows):
            name_label = QLabel(name + "：")
            name_label.setAlignment(Qt.AlignRight | Qt.AlignTop)
            value_label = QLabel("-")
            value_label.setWordWrap(True)
            value_label.setTextInteractionFlags(Qt.TextSelectableByMouse)
            self.value_labels[key] = value_label
            grid.addWidget(name_label, row, 0)
            grid.addWidget(value_label, row, 1)

        right_layout.addLayout(grid)

        raw_title = QLabel("原始 JSON")
        raw_title_font = QFont()
        raw_title_font.setBold(True)
        raw_title.setFont(raw_title_font)
        right_layout.addWidget(raw_title)

        self.raw_json = QPlainTextEdit()
        self.raw_json.setReadOnly(True)
        self.raw_json.setMinimumHeight(180)
        self.raw_json.setStyleSheet("font-family: Consolas, monospace; font-size: 11px;")
        right_layout.addWidget(self.raw_json, 1)

        hint = QLabel("提示：按窗口右上角关闭程序。")
        hint.setStyleSheet("color:#666;")
        right_layout.addWidget(hint)

        main_layout.addWidget(right_panel, 1)

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_ui)
        self.timer.start(33)

    def closeEvent(self, event):
        self.shared.running = False
        event.accept()

    def _set_value(self, key: str, value: Any) -> None:
        label = self.value_labels.get(key)
        if label:
            label.setText(str(value) if value is not None else "-")

    def _update_info_panel(self, video_status: str, qr_status: str, qr_data: Optional[Dict[str, Any]], fetch_time: str, qr_error: str) -> None:
        self.status_label.setText(f"RTSP：{video_status}\nQR：{qr_status}")
        self._set_value("rtsp", video_status)
        self._set_value("qr_api", qr_status)
        self._set_value("fetch_time", fetch_time)

        if not qr_data:
            self._set_value("qr_found", "-")
            self._set_value("qr_text", "-")
            self._set_value("frame_id", "-")
            self._set_value("pts", "-")
            self._set_value("capture_time", "-")
            self._set_value("detect_end_time", "-")
            self._set_value("detect_cost", "-")
            self._set_value("scanned_frames", "-")
            self._set_value("detected_frames", "-")
            self._set_value("queue", "-")
            self._set_value("error", qr_error)
            return

        qr_found = bool(qr_data.get("qr_found", False))
        codes = qr_data.get("codes") or []
        texts = []
        if isinstance(codes, list):
            for code in codes:
                if isinstance(code, dict):
                    text = code.get("text") or code.get("data")
                    if text:
                        texts.append(str(text))

        self._set_value("qr_found", "是" if qr_found else "否")
        self._set_value("qr_text", "\n".join(texts) if texts else "-")
        self._set_value("frame_id", qr_data.get("frame_id", "-"))
        self._set_value("pts", qr_data.get("pts", "-"))
        self._set_value("capture_time", unix_ms_to_text(qr_data.get("capture_unix_ms")))
        self._set_value("detect_end_time", unix_ms_to_text(qr_data.get("detect_end_unix_ms")))
        self._set_value("detect_cost", f"{qr_data.get('detect_cost_ms', '-')} ms")
        self._set_value("scanned_frames", qr_data.get("scanned_frames", "-"))
        self._set_value("detected_frames", qr_data.get("detected_frames", "-"))

        pushed = qr_data.get("queue_pushed", "-")
        dropped = qr_data.get("queue_dropped", "-")
        self._set_value("queue", f"pushed={pushed}, dropped={dropped}")

        error_value = qr_data.get("error") or qr_error
        self._set_value("error", error_value if error_value else "-")

        try:
            json_text = json.dumps(qr_data, ensure_ascii=False, indent=2)
        except Exception:
            json_text = str(qr_data)

        if json_text != self.last_json_text:
            self.raw_json.setPlainText(json_text)
            self.last_json_text = json_text

    def refresh_ui(self) -> None:
        with self.shared.lock:
            frame = None if self.shared.latest_frame_bgr is None else self.shared.latest_frame_bgr.copy()
            video_status = self.shared.video_status
            qr_status = self.shared.qr_status
            qr_data = None if self.shared.latest_qr is None else dict(self.shared.latest_qr)
            fetch_time = self.shared.latest_qr_fetch_time
            qr_error = self.shared.latest_qr_error

        if frame is not None:
            frame = draw_qr_overlay(frame, qr_data)
            pixmap = bgr_to_qpixmap(frame)
            scaled = pixmap.scaled(
                self.video_label.size(),
                Qt.KeepAspectRatio,
                Qt.SmoothTransformation,
            )
            self.video_label.setPixmap(scaled)
        else:
            self.video_label.setText(video_status)

        self._update_info_panel(video_status, qr_status, qr_data, fetch_time, qr_error)


def build_urls(args) -> Tuple[str, str]:
    rtsp_url = args.rtsp
    qr_url = args.qr_url

    if not rtsp_url:
        rtsp_url = f"rtsp://{args.ip}:{args.rtsp_port}/{args.rtsp_path.lstrip('/')}"

    if not qr_url:
        qr_url = f"http://{args.ip}:{args.http_port}/api/qr/latest"

    return rtsp_url, qr_url


def parse_args():
    parser = argparse.ArgumentParser(description="reCamera QR PyQt Viewer")
    parser.add_argument("--ip", default="192.168.4.5", help="reCamera IP address")
    parser.add_argument("--rtsp", default="", help="Full RTSP URL, e.g. rtsp://192.168.4.5:8554/live0")
    parser.add_argument("--qr-url", default="", help="Full QR API URL, e.g. http://192.168.4.5:8080/api/qr/latest")
    parser.add_argument("--rtsp-port", type=int, default=8554)
    parser.add_argument("--rtsp-path", default="live0", help="RTSP path, usually live0 or onvif")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--qr-interval", type=float, default=0.2, help="QR polling interval in seconds")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rtsp_url, qr_url = build_urls(args)

    shared = SharedState()

    rtsp_worker = RtspWorker(shared, rtsp_url)
    qr_worker = QrPollWorker(shared, qr_url, interval_sec=args.qr_interval)

    rtsp_worker.start()
    qr_worker.start()

    app = QApplication(sys.argv)
    window = MainWindow(shared, rtsp_url, qr_url)
    window.show()

    ret = app.exec_()

    shared.running = False
    time.sleep(0.2)
    return ret


if __name__ == "__main__":
    sys.exit(main())
