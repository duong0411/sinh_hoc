# -*- coding: utf-8 -*-
"""So sánh số OLED (Serial ESP) với MQTT topics app đang subscribe."""
import json
import os
import re
import socket
import ssl
import struct
import threading
import time
import base64

import serial

HOST = "mqtt.duynguyen.io.vn"
PATH = "/mqtt"
TOPICS = [
    b"tele/800/status",
    b"tele/800_temp/status",
    b"tele/800_humi/status",
    b"tele/800_co2/status",
]

mqtt_vals = {}
mqtt_lock = threading.Lock()
stop = False


def ws_mask(data: bytes) -> bytes:
    key = os.urandom(4)
    masked = bytes(b ^ key[i % 4] for i, b in enumerate(data))
    return key + masked


def ws_frame(opcode: int, payload: bytes) -> bytes:
    ln = len(payload)
    if ln < 126:
        hdr = bytes([0x80 | opcode, 0x80 | ln])
    else:
        hdr = bytes([0x80 | opcode, 0x80 | 126]) + struct.pack("!H", ln)
    return hdr + ws_mask(payload)


def mqtt_connect_pkt(cid: str) -> bytes:
    proto = b"\x00\x04MQTT\x04\x02\x00\x28"
    cid_b = cid.encode()
    vh = proto + struct.pack("!H", len(cid_b)) + cid_b
    return bytes([0x10, len(vh)]) + vh


def mqtt_sub_pkt(pid: int, topic: bytes) -> bytes:
    body = struct.pack("!H", pid) + struct.pack("!H", len(topic)) + topic + b"\x00"
    return bytes([0x82, len(body)]) + body


def parse_publish(p: bytes):
    i = 1
    mul = 1
    rem = 0
    while True:
        c = p[i]
        rem += (c & 127) * mul
        mul *= 128
        i += 1
        if not (c & 128):
            break
    tlen = struct.unpack("!H", p[i : i + 2])[0]
    i += 2
    topic = p[i : i + tlen].decode("utf-8", "replace")
    i += tlen
    qos = (p[0] >> 1) & 3
    if qos:
        i += 2
    payload = p[i:].decode("utf-8", "replace")
    return topic, payload


def mqtt_thread():
    global stop
    try:
        ctx = ssl.create_default_context()
        raw = socket.create_connection((HOST, 443), timeout=10)
        sock = ctx.wrap_socket(raw, server_hostname=HOST)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            f"GET {PATH} HTTP/1.1\r\nHost: {HOST}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: mqtt\r\n\r\n"
        )
        sock.sendall(req.encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += sock.recv(4096)
        if b"101" not in buf.split(b"\r\n", 1)[0]:
            print("MQTT_PROBE: handshake fail")
            return
        print("MQTT_PROBE: WSS OK")
        sock.sendall(ws_frame(2, mqtt_connect_pkt("syncchk_" + str(int(time.time())))))
        time.sleep(0.35)
        for i, t in enumerate(TOPICS):
            sock.sendall(ws_frame(2, mqtt_sub_pkt(i + 1, t)))
        print("MQTT_PROBE: subscribed tele/800_*")
        sock.settimeout(0.4)
        acc = b""
        while not stop:
            try:
                chunk = sock.recv(8192)
                if not chunk:
                    break
                acc += chunk
            except socket.timeout:
                continue
            while len(acc) >= 2:
                b1, b2 = acc[0], acc[1]
                opcode = b1 & 0x0F
                masked = bool(b2 & 0x80)
                ln = b2 & 0x7F
                hdr = 2
                if ln == 126:
                    if len(acc) < 4:
                        break
                    ln = struct.unpack("!H", acc[2:4])[0]
                    hdr = 4
                need = hdr + (4 if masked else 0) + ln
                if len(acc) < need:
                    break
                payload = acc[hdr + (4 if masked else 0) : need]
                if masked:
                    mk = acc[hdr : hdr + 4]
                    payload = bytes(payload[j] ^ mk[j % 4] for j in range(len(payload)))
                acc = acc[need:]
                if opcode in (0, 2) and payload and (payload[0] >> 4) == 3:
                    try:
                        topic, pl = parse_publish(payload)
                        with mqtt_lock:
                            mqtt_vals[topic] = (pl, time.time())
                    except Exception:
                        pass
        sock.close()
    except Exception as e:
        print("MQTT_PROBE ERR", e)


def parse_value(entry):
    if not entry:
        return None
    pl = entry[0].strip()
    try:
        if pl.startswith("{"):
            return json.loads(pl).get("value")
        if pl.lower() in ("online", "offline"):
            return pl.lower()
        return float(pl)
    except Exception:
        return pl


def age(entry):
    if not entry:
        return None
    return round(time.time() - entry[1], 2)


def main():
    global stop
    th = threading.Thread(target=mqtt_thread, daemon=True)
    th.start()
    time.sleep(1.2)

    ser = serial.Serial("COM4", 115200, timeout=0.4)
    print("=== LIVE OLED(Serial) vs MQTT(app topics) ===")
    end = time.time() + 20
    sync_ok = 0
    samples = 0
    while time.time() < end:
        line = ser.readline()
        if not line:
            continue
        s = line.decode("utf-8", "replace").rstrip()
        m = re.search(r"T=([0-9.]+)C\s+H=([0-9.]+)%\s+CO2=(\d+)", s)
        if m:
            t, h, c = float(m.group(1)), float(m.group(2)), int(m.group(3))
            samples += 1
            with mqtt_lock:
                mt = mqtt_vals.get("tele/800_temp/status")
                mh = mqtt_vals.get("tele/800_humi/status")
                mc = mqtt_vals.get("tele/800_co2/status")
                mo = mqtt_vals.get("tele/800/status")
            app_t, app_h, app_c = parse_value(mt), parse_value(mh), parse_value(mc)
            same_c = app_c is not None and abs(float(app_c) - c) <= 1
            same_t = app_t is not None and abs(float(app_t) - t) <= 0.2
            same_h = app_h is not None and abs(float(app_h) - h) <= 1.5
            # MQTT age: realtime nếu < 3s
            ages = (age(mt), age(mh), age(mc))
            fresh = all(a is not None and a <= 3.0 for a in ages)
            if same_c and same_t and same_h and fresh:
                flag = "SYNC_REALTIME"
                sync_ok += 1
            elif same_c and same_t and same_h:
                flag = "SYNC_BUT_STALE"
            else:
                flag = "DIFF/LAG"
            print(
                f"OLED T={t} H={h} CO2={c} | "
                f"APP T={app_t} H={app_h} CO2={app_c} online={parse_value(mo)} | "
                f"age_s t={ages[0]} h={ages[1]} c={ages[2]} | {flag}"
            )
        elif any(k in s for k in ("[MQTT]", "[WSS]", "[PORTAL]", "[WiFi]", "MQTT WSS")):
            print(s)

    ser.close()
    stop = True
    time.sleep(0.2)
    print("---")
    print(f"samples={samples} sync_realtime={sync_ok}")
    if samples == 0:
        print("RESULT: ESP khong in so do (portal / mat Serial?)")
    elif sync_ok >= max(1, samples // 2):
        print("RESULT: OLED <-> MQTT DONG BO REALTIME OK")
    elif sync_ok > 0:
        print("RESULT: DONG BO MOT PHAN — thinh thoang lag")
    else:
        with mqtt_lock:
            print("mqtt_seen=", {k: v[0] for k, v in mqtt_vals.items()})
        print("RESULT: CHUA DONG BO — ESP offline MQTT hoac app topic lech")


if __name__ == "__main__":
    main()
