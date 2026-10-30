# -*- coding: utf-8 -*-
"""OneNET token generator + one-click connection tester (GUI).

Double-click 生成Token工具.bat to run (Python + tkinter, no extra packages).
Fill in 产品ID / 设备名称 / 设备密钥 / 有效天数, press 生成 Token to get the
lines for mqtt_cfg.h, press 测试连接 to verify the same parameters against the
real OneNET broker before flashing.

The device key is typed in by the user on every run: this program neither reads
nor writes any file, so no secret is stored next to the project. The test log
never prints the token or raw frames.
"""
import base64
import hashlib
import hmac
import json
import queue
import socket
import struct
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
from urllib.parse import quote

DEFAULT_SERVER = "183.230.40.96"
DEFAULT_PORT = "1883"
TEST_ROLL = 12.345
TEST_PITCH = -5.432
CONNACK_TEXT = {
    0: "接受",
    1: "协议版本不被支持",
    2: "客户端 ID 非法",
    3: "服务不可用",
    4: "用户名或密码错误（token 不对 / 设备不在 MQTT3.x 集群）",
    5: "未授权",
}


def gen_token(pid: str, dev: str, key_b64: str, days: float) -> str:
    et = int(time.time() + days * 86400)
    res = f"products/{pid}/devices/{dev}"
    str_for_sig = f"{et}\nmd5\n{res}\n2018-10-31"
    key = base64.b64decode(key_b64)
    sign = base64.b64encode(
        hmac.new(key, str_for_sig.encode(), hashlib.md5).digest()
    ).decode()
    return (f"version=2018-10-31&res={quote(res, safe='')}&et={et}"
            f"&method=md5&sign={quote(sign, safe='')}")


def _remaining(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n % 128
        n //= 128
        out.append(b | 0x80 if n else b)
        if not n:
            return bytes(out)


def _field(s: str) -> bytes:
    b = s.encode()
    return struct.pack(">H", len(b)) + b


def _packet(hdr: int, payload: bytes) -> bytes:
    return bytes([hdr]) + _remaining(len(payload)) + payload


def _read_packet(sock, buf):
    def fill(n):
        nonlocal buf
        while len(buf) < n:
            chunk = sock.recv(4096)
            if not chunk:
                raise ConnectionError("服务器断开了连接")
            buf += chunk
        return buf

    fill(2)
    hdr = buf[0]
    pos = 1
    length = 0
    mult = 1
    while True:
        fill(pos + 1)
        b = buf[pos]
        pos += 1
        length += (b & 0x7F) * mult
        mult *= 128
        if not b & 0x80:
            break
    fill(pos + length)
    return hdr, buf[pos:pos + length], buf[pos + length:]


def _wait_type(sock, buf, want, log):
    sock.settimeout(10)
    while True:
        hdr, body, buf = _read_packet(sock, buf)
        kind = hdr >> 4
        if kind == want:
            return hdr, body, buf
        log(f"（收到别的报文 type={kind}，先忽略）")


def test_connection(server, port, pid, dev, token, log) -> bool:
    pub = f"$sys/{pid}/{dev}/thing/property/post"
    reply = pub + "/reply"
    var = _field("MQTT") + bytes([4, 0xC2, 0x00, 60])
    payload = _field(dev) + _field(pid) + _field(token)

    sock = socket.create_connection((server, port), timeout=8)
    sock.settimeout(10)
    log(f"1) TCP 已连上 {server}:{port}", "ok")
    try:
        sock.sendall(_packet(0x10, var + payload))
        _, body, buf = _wait_type(sock, b"", 2, log)
        rc = body[1] if len(body) > 1 else 255
        if rc != 0:
            log(f"2) MQTT 连接被拒：rc={rc} {CONNACK_TEXT.get(rc, '')}", "bad")
            return False
        log("2) MQTT 连接成功（CONNACK rc=0，token 有效）", "ok")

        sock.sendall(_packet(0x82, struct.pack(">H", 1) + _field(reply) + b"\x00"))
        _, body, buf = _wait_type(sock, buf, 9, log)
        if body[:2] != b"\x00\x01" or body[-1:] == b"\x80":
            log("3) 订阅回复 topic 失败", "bad")
            return False
        log(f"3) 已订阅 {reply}", "ok")

        msg = json.dumps({"id": "1", "version": "1.0", "params": {
            "Roll": {"value": TEST_ROLL}, "Pitch": {"value": TEST_PITCH}}},
            separators=(",", ":")).encode()
        sock.sendall(_packet(0x30, _field(pub) + msg))
        log(f"4) 已发布测试数据 Roll={TEST_ROLL} Pitch={TEST_PITCH}（平台会多出这条记录）")
        hdr, body, buf = _wait_type(sock, buf, 3, log)
        off = 2 if (hdr >> 1) & 3 else 0
        topic_len = struct.unpack(">H", body[off:off + 2])[0]
        text = body[off + 2 + topic_len:].decode("utf-8", "replace")
        try:
            back = json.loads(text)
            code, note = back.get("code"), back.get("msg")
        except ValueError:
            code, note = None, text[:60]
        if code == 200:
            log("5) 平台回 code=200 success，物模型和数据格式都对", "ok")
        else:
            log(f"5) 平台回 code={code} msg={note}"
                "（2306=标识符不对/没发布，2211=属性类型不符，2407=version 不对）", "bad")
            return False

        sock.sendall(_packet(0xC0, b""))
        _wait_type(sock, buf, 13, log)
        log("6) 保活 PINGRESP 正常", "ok")
        return True
    finally:
        sock.close()


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Token 生成器（含连接测试）")
        self.geometry("660x540")
        self.resizable(False, False)

        frm = ttk.Frame(self, padding=12)
        frm.pack(fill="both", expand=True)

        self.e_pid = ttk.Entry(frm, width=36)
        self.e_dev = ttk.Entry(frm, width=36)
        self.e_key = ttk.Entry(frm, width=36, show="*")
        self.e_days = ttk.Entry(frm, width=10)
        self.e_days.insert(0, "3650")
        self.e_server = ttk.Entry(frm, width=36)
        self.e_server.insert(0, DEFAULT_SERVER)
        self.e_port = ttk.Entry(frm, width=10)
        self.e_port.insert(0, DEFAULT_PORT)

        rows = [("产品ID", self.e_pid), ("设备名称", self.e_dev),
                ("设备密钥", self.e_key), ("有效天数", self.e_days),
                ("服务器", self.e_server), ("端口", self.e_port)]
        for i, (label, w) in enumerate(rows):
            ttk.Label(frm, text=label + "：").grid(row=i, column=0, sticky="e", pady=3)
            w.grid(row=i, column=1, sticky="w", pady=3)

        self.b_gen = ttk.Button(frm, text="生成 Token", command=self.on_gen)
        self.b_gen.grid(row=0, column=2, rowspan=2, padx=8, sticky="ns")
        self.b_test = ttk.Button(frm, text="测试连接", command=self.on_test)
        self.b_test.grid(row=2, column=2, rowspan=2, padx=8, sticky="ns")
        self.b_copy = ttk.Button(frm, text="复制结果", command=self.on_copy)
        self.b_copy.grid(row=4, column=2, rowspan=2, padx=8, sticky="ns")

        mid = ttk.Frame(frm)
        mid.grid(row=6, column=0, columnspan=3, sticky="nsew", pady=(8, 0))
        frm.rowconfigure(6, weight=1)
        frm.columnconfigure(1, weight=1)
        self.txt = tk.Text(mid, height=7, wrap="char")
        self.txt.pack(fill="both", expand=True)

        self.txt_log = tk.Text(mid, height=9, wrap="char", state="disabled")
        self.txt_log.pack(fill="both", expand=True, pady=(6, 0))
        self.txt_log.tag_config("ok", foreground="#0a7d28")
        self.txt_log.tag_config("bad", foreground="#b00020")

        self._q = queue.Queue()
        self.after(100, self._drain)

        self._menu = tk.Menu(self, tearoff=0)
        for label, ev in (("粘贴", "<<Paste>>"), ("复制", "<<Copy>>"),
                          ("剪切", "<<Cut>>"), ("全选", "<<SelectAll>>")):
            self._menu.add_command(label=label,
                                   command=lambda ev=ev: self._target.event_generate(ev))
        widgets = [self.e_pid, self.e_dev, self.e_key, self.e_days,
                   self.e_server, self.e_port, self.txt]
        for w in widgets:
            w.bind("<Button-3>", self.on_popup)

    def on_popup(self, event):
        self._target = event.widget
        self._menu.tk_popup(event.x_root, event.y_root)

    def _drain(self):
        try:
            while True:
                msg, tag = self._q.get_nowait()
                if tag == "__done__":
                    self.b_test.configure(state="normal")
                    continue
                self.txt_log.configure(state="normal")
                self.txt_log.insert("end", msg + "\n", tag)
                self.txt_log.see("end")
                self.txt_log.configure(state="disabled")
        except queue.Empty:
            pass
        self.after(100, self._drain)

    def log(self, msg, tag=""):
        self._q.put((msg, tag))

    def _read_form(self):
        pid = self.e_pid.get().strip()
        dev = self.e_dev.get().strip()
        key = self.e_key.get().strip()
        days = float(self.e_days.get().strip() or 3650)
        server = self.e_server.get().strip() or DEFAULT_SERVER
        port = int(self.e_port.get().strip() or DEFAULT_PORT)
        return pid, dev, key, days, server, port

    def on_gen(self):
        try:
            pid, dev, key, days, _, _ = self._read_form()
            token = gen_token(pid, dev, key, days)
        except Exception as e:
            messagebox.showerror("出错", f"生成失败：{e}\n\n检查产品ID/设备名/设备密钥是否填写正确")
            return
        exp = time.strftime("%Y-%m-%d", time.gmtime(time.time() + days * 86400))
        out = (f"到期日：{exp}\n\n"
               f"Token：\n{token}\n\n"
               f"—— 粘贴到 mqtt_cfg.h ——\n"
               f'#define ONEONET_CLIENT_ID     "{dev}"\n'
               f'#define ONEONET_USERNAME      "{pid}"\n'
               f'#define ONEONET_PASSWORD      "{token}"\n'
               f'#define MQTT_PUB_TOPIC        "$sys/{pid}/{dev}/thing/property/post"\n')
        self.txt.delete("1.0", "end")
        self.txt.insert("1.0", out)

    def on_test(self):
        try:
            pid, dev, key, days, server, port = self._read_form()
            token = gen_token(pid, dev, key, days)
        except Exception as e:
            messagebox.showerror("出错", f"参数有问题：{e}\n\n"
                                   "先填 产品ID / 设备名称 / 设备密钥（密钥从 OneNET 设备详情页原样复制）")
            return
        self.txt_log.configure(state="normal")
        self.txt_log.delete("1.0", "end")
        self.txt_log.configure(state="disabled")
        self.b_test.configure(state="disabled")
        threading.Thread(target=self._worker,
                         args=(server, port, pid, dev, token), daemon=True).start()

    def _worker(self, server, port, pid, dev, token):
        try:
            ok = test_connection(server, port, pid, dev, token, self.log)
        except Exception as e:
            self.log(f"失败：{e}", "bad")
            ok = False
        self.log("结论：可以用，直接照抄进 mqtt_cfg.h 烧板" if ok else "结论：没通过，看上面红字那一步",
                 "ok" if ok else "bad")
        self.log("", "__done__")

    def on_copy(self):
        self.clipboard_clear()
        self.clipboard_append(self.txt.get("1.0", "end").rstrip())
        self.update()
        messagebox.showinfo("已复制", "内容已复制到剪贴板")


if __name__ == "__main__":
    App().mainloop()
