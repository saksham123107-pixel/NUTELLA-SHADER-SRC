"""
nutella — horizontal injector + live controller
Run as Administrator:  python nutella.py
"""
import ctypes, os, sys, time, mmap, struct
from ctypes import wintypes

from PyQt6.QtCore import Qt, QThread, pyqtSignal, QTimer
from PyQt6.QtGui import QFont, QColor, QPainter
from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QLabel, QPushButton, QPlainTextEdit, QFrame, QLineEdit,
)

# ─────────────────────────────────────────────────────────────────────────
# CONFIG
# ─────────────────────────────────────────────────────────────────────────
HERE   = os.path.dirname(os.path.abspath(__file__))
DLL    = os.path.join(HERE, "nutella_shader.dll")
LOG    = os.path.join(HERE, "shader_log.txt")
TARGET = "HD-Player.exe"
SHM    = "Local\\NutellaParams"

PARAMS_FMT  = "<10f2i"
PARAMS_SIZE = struct.calcsize(PARAMS_FMT)

# ─────────────────────────────────────────────────────────────────────────
# SHARED MEMORY
# ─────────────────────────────────────────────────────────────────────────
class SharedParams:
    def __init__(self):
        self._buf = None
        try:
            self._buf = mmap.mmap(-1, PARAMS_SIZE, tagname=SHM)
        except Exception as e:
            print(f"[!] shared memory open failed: {e}")
            print("[!] another instance may be running")
            raise

    def write(self, sat, con, bri, shad, warm, blm, lift, hdr, amb, vig, en, fx):
        if self._buf is None: return
        try:
            self._buf.seek(0)
            self._buf.write(struct.pack(PARAMS_FMT,
                float(sat), float(con), float(bri), float(shad), float(warm),
                float(blm), float(lift), float(hdr), float(amb), float(vig),
                int(en), int(fx)))
        except Exception as e:
            print(f"[!] shm write failed: {e}")

    def close(self):
        if self._buf is not None:
            try: self._buf.close()
            except Exception: pass

# ─────────────────────────────────────────────────────────────────────────
# WIN32 INJECTOR
# ─────────────────────────────────────────────────────────────────────────
k32 = ctypes.WinDLL("kernel32", use_last_error=True)

class PE(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long), ("dwFlags", wintypes.DWORD),
        ("szExeFile", wintypes.WCHAR * 260)]

k32.OpenProcess.restype = wintypes.HANDLE
k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k32.VirtualAllocEx.restype = ctypes.c_void_p
k32.VirtualAllocEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
k32.WriteProcessMemory.restype = wintypes.BOOL
k32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.GetModuleHandleW.restype = wintypes.HMODULE
k32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
k32.GetProcAddress.restype = ctypes.c_void_p
k32.GetProcAddress.argtypes = [wintypes.HMODULE, wintypes.LPCSTR]
k32.CreateRemoteThread.restype = wintypes.HANDLE
k32.CreateRemoteThread.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
k32.WaitForSingleObject.restype = wintypes.DWORD
k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
k32.GetExitCodeThread.restype = wintypes.BOOL
k32.GetExitCodeThread.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
k32.CloseHandle.restype = wintypes.BOOL
k32.CloseHandle.argtypes = [wintypes.HANDLE]
k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
k32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
k32.Process32FirstW.restype = wintypes.BOOL
k32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PE)]
k32.Process32NextW.restype = wintypes.BOOL
k32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PE)]

def find_pid(name):
    snap = k32.CreateToolhelp32Snapshot(0x2, 0)
    if snap == ctypes.c_void_p(-1).value: return 0
    e = PE(); e.dwSize = ctypes.sizeof(e)
    try:
        if not k32.Process32FirstW(snap, ctypes.byref(e)): return 0
        while True:
            if e.szExeFile.lower() == name.lower():
                return e.th32ProcessID
            if not k32.Process32NextW(snap, ctypes.byref(e)): return 0
    finally:
        k32.CloseHandle(snap)

def inject(pid, dll):
    dll = os.path.abspath(dll)
    b = dll.encode("utf-16-le") + b"\x00\x00"
    h = k32.OpenProcess(0x1F0FFF, False, pid)
    if not h: return False, f"OpenProcess failed: {ctypes.get_last_error()}"
    try:
        addr = k32.VirtualAllocEx(h, None, len(b), 0x3000, 0x04)
        if not addr: return False, "VirtualAllocEx failed"
        w = ctypes.c_size_t(0)
        if not k32.WriteProcessMemory(h, addr, b, len(b), ctypes.byref(w)):
            return False, "WriteProcessMemory failed"
        p = k32.GetProcAddress(k32.GetModuleHandleW("kernel32.dll"), b"LoadLibraryW")
        if not p: return False, "LoadLibraryW not found"
        t = wintypes.DWORD(0)
        th = k32.CreateRemoteThread(h, None, 0, p, addr, 0, ctypes.byref(t))
        if not th: return False, "CreateRemoteThread failed"
        k32.WaitForSingleObject(th, 0xFFFFFFFF)
        code = wintypes.DWORD(0)
        k32.GetExitCodeThread(th, ctypes.byref(code))
        k32.CloseHandle(th)
        if code.value == 0:
            return False, "LoadLibraryW returned NULL"
        return True, f"loaded @ 0x{code.value:X}"
    finally:
        k32.CloseHandle(h)


class InjectWorker(QThread):
    log = pyqtSignal(str, str)
    finished = pyqtSignal(bool)

    def __init__(self, target):
        super().__init__()
        self.target = target

    def run(self):
        try:
            if not os.path.exists(DLL):
                self.log.emit(f"DLL missing: {DLL}", "err")
                self.finished.emit(False); return

            self.log.emit(f"DLL: {os.path.basename(DLL)} ({os.path.getsize(DLL):,} bytes)", "info")
            self.log.emit(f"looking for {self.target}…", "info")

            pid = find_pid(self.target)
            if not pid:
                self.log.emit(f"{self.target} not running — start BlueStacks first", "err")
                self.finished.emit(False); return

            self.log.emit(f"found pid {pid}", "ok")
            self.log.emit("injecting…", "info")

            ok, msg = inject(pid, DLL)
            if not ok:
                self.log.emit(msg, "err")
                self.finished.emit(False); return

            self.log.emit(f"injected ({msg})", "ok")
            time.sleep(1.5)

            if os.path.exists(LOG):
                self.log.emit("— shader_log.txt —", "info")
                try:
                    with open(LOG, "r", errors="replace") as f:
                        for line in f.read().rstrip().splitlines()[-20:]:
                            self.log.emit(line, "info")
                except Exception as e:
                    self.log.emit(f"log read error: {e}", "err")
            else:
                self.log.emit("(no log yet)", "info")

            self.finished.emit(True)
        except Exception as e:
            self.log.emit(f"worker crash: {e}", "err")
            self.finished.emit(False)


# ─────────────────────────────────────────────────────────────────────────
# PALETTE
# ─────────────────────────────────────────────────────────────────────────
BG="#0c0c0c"; SURFACE="#141414"; SURFACE2="#1a1a1a"
BORDER="#232323"; BORDER_HI="#2e2e2e"
TEXT="#d6d6d6"; TEXT_HI="#f0f0f0"
DIM="#7a7a7a"; DIM2="#5a5a5a"
OK="#8fbf8f"; ERR="#c97777"; ACCENT="#8fb8ff"

# ─────────────────────────────────────────────────────────────────────────
# SLIDER WIDGET
# ─────────────────────────────────────────────────────────────────────────
class Slider(QWidget):
    changed = pyqtSignal(str, float)

    def __init__(self, key, label, lo, hi, init, fmt="{:.2f}"):
        super().__init__()
        self._key = key
        self._lo, self._hi = lo, hi
        self._val = float(init)
        self._fmt = fmt
        self._label = label
        self._hot = False
        self._pressed = False
        self.setFixedHeight(48)

    def value(self): return self._val

    def setValue(self, v):
        v = max(self._lo, min(self._hi, float(v)))
        if v == self._val: return
        self._val = v
        self.changed.emit(self._key, v)
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        p.setPen(QColor(TEXT_HI if (self._hot or self._pressed) else TEXT))
        p.setFont(QFont("Segoe UI", 9, QFont.Weight.Bold))
        p.drawText(0, 15, self._label)

        p.setPen(QColor(ACCENT))
        p.setFont(QFont("Consolas", 9, QFont.Weight.Bold))
        p.drawText(self.width() - 56, 15, 56, 15,
                   Qt.AlignmentFlag.AlignRight, self._fmt.format(self._val))

        track = self.rect().adjusted(0, 28, 0, 0)
        track.setHeight(4)
        p.setBrush(QColor(SURFACE2))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawRoundedRect(track, 2, 2)

        t = (self._val - self._lo) / (self._hi - self._lo)
        fill = track.adjusted(0, 0, int(-(1 - t) * track.width()), 0)
        p.setBrush(QColor(ACCENT))
        p.drawRoundedRect(fill, 2, 2)

        kx = track.x() + int(t * track.width())
        ky = track.y() + 2
        p.setBrush(QColor("#f0f0f0"))
        p.drawEllipse(kx - 6, ky - 6, 12, 12)

    def mousePressEvent(self, e):
        self._pressed = True
        self._set(e.position().x())
        self.update()

    def mouseMoveEvent(self, e):
        if e.buttons() & Qt.MouseButton.LeftButton:
            self._set(e.position().x())

    def mouseReleaseEvent(self, e):
        self._pressed = False
        self.update()

    def enterEvent(self, _): self._hot = True; self.update()
    def leaveEvent(self, _): self._hot = False; self.update()

    def _set(self, x):
        t = max(0.0, min(1.0, x / max(1, self.width())))
        self.setValue(self._lo + t * (self._hi - self._lo))


# ─────────────────────────────────────────────────────────────────────────
# MAIN WINDOW
# ─────────────────────────────────────────────────────────────────────────
class Nutella(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("nutella")
        self.resize(980, 620)

        # shared memory
        try:
            self.params = SharedParams()
        except Exception:
            QApplication.instance().quit()
            return

        self._worker = None

        # timer — push params every 100ms so DLL always sees latest
        self._tick = QTimer(self)
        self._tick.timeout.connect(self._push)
        self._tick.start(100)

        self._build()
        self._style()
        self._push()

    # ─── build ──────────────────────────────────────────────────────────
    def _build(self):
        root = QHBoxLayout(self)
        root.setContentsMargins(20, 20, 20, 20)
        root.setSpacing(18)

        # ═══ LEFT — controls ═══
        left = QVBoxLayout()
        left.setSpacing(10)

        t = QLabel("NUTELLA"); t.setObjectName("title")
        left.addWidget(t)
        s = QLabel("live shader controller"); s.setObjectName("sub")
        left.addWidget(s)

        sep = QFrame(); sep.setObjectName("sep"); sep.setFixedHeight(1)
        left.addWidget(sep)

        row = QHBoxLayout(); row.setSpacing(8)
        lbl = QLabel("TARGET"); lbl.setObjectName("field")
        row.addWidget(lbl)
        self.le_target = QLineEdit(TARGET); self.le_target.setObjectName("target")
        row.addWidget(self.le_target, 1)
        left.addLayout(row)

        brow = QHBoxLayout(); brow.setSpacing(8)
        self.btn_inject = QPushButton("INJECT")
        self.btn_inject.setObjectName("primary")
        self.btn_inject.clicked.connect(self._inject)
        brow.addWidget(self.btn_inject, 2)

        self.btn_reset = QPushButton("RESET")
        self.btn_reset.setObjectName("ghost")
        self.btn_reset.clicked.connect(self._reset)
        brow.addWidget(self.btn_reset, 1)
        left.addLayout(brow)

        self.lbl_status = QLabel("IDLE"); self.lbl_status.setObjectName("status")
        left.addWidget(self.lbl_status)

        sep2 = QFrame(); sep2.setObjectName("sep"); sep2.setFixedHeight(1)
        left.addWidget(sep2)

        plbl = QLabel("PRESETS"); plbl.setObjectName("field")
        left.addWidget(plbl)
        prow = QHBoxLayout(); prow.setSpacing(6)
        for name in ("NEUTRAL", "CINEMA", "VIVID", "COLD", "WARM"):
            b = QPushButton(name); b.setObjectName("preset")
            b.clicked.connect(lambda _, n=name: self._preset(n))
            prow.addWidget(b)
        left.addLayout(prow)

        left.addStretch(1)

        # ═══ MIDDLE — sliders (2 columns) ═══
        mid = QVBoxLayout()
        mid.setSpacing(6)
        hdr = QLabel("SHADER"); hdr.setObjectName("field")
        mid.addWidget(hdr)

        grid = QGridLayout()
        grid.setHorizontalSpacing(22)
        grid.setVerticalSpacing(2)

        self.sliders = {}
        specs = [
            ("saturation", "SATURATION",  0.0,  2.0, 1.0),
            ("contrast",   "CONTRAST",    0.5,  2.0, 1.0),
            ("brightness", "BRIGHTNESS", -0.5,  0.5, 0.0),
            ("shadow",     "SHADOW",      0.0,  1.0, 0.0),
            ("warmth",     "WARMTH",     -1.0,  1.0, 0.0),
            ("bloom",      "BLOOM",       0.0,  1.5, 0.0),
            ("lift",       "LIFT",        0.0,  1.0, 0.0),
            ("hdr",        "HDR",         0.0,  1.5, 0.0),
            ("ambient",    "AMBIENT",     0.0,  1.0, 0.0),
            ("vignette",   "VIGNETTE",    0.0,  1.0, 0.0),
        ]
        for i, (key, label, lo, hi, init) in enumerate(specs):
            sl = Slider(key, label, lo, hi, init)
            sl.changed.connect(lambda _k, _v: self._push())
            self.sliders[key] = sl
            grid.addWidget(sl, i // 2, i % 2)
        mid.addLayout(grid)
        mid.addStretch(1)

        # ═══ RIGHT — log ═══
        right = QVBoxLayout()
        right.setSpacing(6)
        rlbl = QLabel("LOG"); rlbl.setObjectName("field")
        right.addWidget(rlbl)
        self.log = QPlainTextEdit()
        self.log.setObjectName("logbox")
        self.log.setReadOnly(True)
        right.addWidget(self.log, 1)

        # ═══ ASSEMBLE ═══
        left_w = QWidget(); left_w.setLayout(left); left_w.setFixedWidth(290)
        mid_w  = QWidget(); mid_w.setLayout(mid);   mid_w.setFixedWidth(400)
        right_w = QWidget(); right_w.setLayout(right)

        root.addWidget(left_w, 0)
        root.addWidget(mid_w, 0)
        root.addWidget(right_w, 1)

    # ─── style ──────────────────────────────────────────────────────────
    def _style(self):
        self.setStyleSheet(f"""
            QWidget {{ background: {BG}; color: {TEXT};
                       font-family: 'Segoe UI'; font-weight: bold; }}
            QLabel#title {{ color: {TEXT_HI}; font-size: 20px; letter-spacing: 2px; }}
            QLabel#sub {{ color: {DIM}; font-size: 11px; }}
            QLabel#field {{ color: {DIM}; font-size: 10px; letter-spacing: 1.5px; }}
            QLabel#status {{ color: {DIM2}; font-family: 'Consolas'; font-size: 11px; }}
            QFrame#sep {{ background: {BORDER}; border: none; }}

            QLineEdit#target {{
                background: {SURFACE}; color: {TEXT_HI};
                border: 1px solid {BORDER}; border-radius: 5px;
                padding: 6px 10px;
                font-family: 'Consolas'; font-size: 11px;
            }}
            QLineEdit#target:focus {{ border-color: {ACCENT}; }}

            QPushButton#primary {{
                background: {ACCENT}; color: #0c0c0c;
                border: none; border-radius: 5px;
                padding: 10px 16px;
                font-weight: bold; font-size: 12px; letter-spacing: 1px;
            }}
            QPushButton#primary:hover {{ background: #a7c9ff; }}
            QPushButton#primary:pressed {{ background: #7aa5e8; }}
            QPushButton#primary:disabled {{ background: {SURFACE}; color: {DIM}; }}

            QPushButton#ghost {{
                background: {SURFACE}; color: {TEXT};
                border: 1px solid {BORDER}; border-radius: 5px;
                padding: 10px 16px;
                font-size: 11px; letter-spacing: 1px;
            }}
            QPushButton#ghost:hover {{ background: {SURFACE2};
                                       border-color: {BORDER_HI}; color: {TEXT_HI}; }}

            QPushButton#preset {{
                background: {SURFACE}; color: {DIM};
                border: 1px solid {BORDER}; border-radius: 4px;
                padding: 6px 4px;
                font-size: 9px; letter-spacing: 1px;
            }}
            QPushButton#preset:hover {{ background: {SURFACE2};
                                        border-color: {ACCENT}; color: {ACCENT}; }}

            QPlainTextEdit#logbox {{
                background: {SURFACE}; color: {TEXT};
                border: 1px solid {BORDER}; border-radius: 5px;
                padding: 8px;
                font-family: 'Consolas'; font-size: 10px;
            }}
        """)

    # ─── push params to DLL ────────────────────────────────────────────
    def _push(self):
        try:
            self.params.write(
                sat  = self.sliders["saturation"].value(),
                con  = self.sliders["contrast"].value(),
                bri  = self.sliders["brightness"].value(),
                shad = self.sliders["shadow"].value(),
                warm = self.sliders["warmth"].value(),
                blm  = self.sliders["bloom"].value(),
                lift = self.sliders["lift"].value(),
                hdr  = self.sliders["hdr"].value(),
                amb  = self.sliders["ambient"].value(),
                vig  = self.sliders["vignette"].value(),
                en   = 1, fx = 0,
            )
        except Exception:
            pass

    def _reset(self):
        defaults = {"saturation":1.0,"contrast":1.0,"brightness":0.0,
                    "shadow":0.0,"warmth":0.0,"bloom":0.0,
                    "lift":0.0,"hdr":0.0,"ambient":0.0,"vignette":0.0}
        for k, v in defaults.items():
            self.sliders[k].setValue(v)
        self._push()

    def _preset(self, name):
        presets = {
            "NEUTRAL": {"saturation":1.0,"contrast":1.0,"brightness":0.0,
                        "shadow":0.0,"warmth":0.0,"bloom":0.0,
                        "lift":0.0,"hdr":0.0,"ambient":0.0,"vignette":0.0},
            "CINEMA":  {"saturation":0.85,"contrast":1.25,"brightness":-0.05,
                        "shadow":0.3,"warmth":0.15,"bloom":0.2,
                        "lift":0.1,"hdr":0.3,"ambient":0.15,"vignette":0.35},
            "VIVID":   {"saturation":1.5,"contrast":1.15,"brightness":0.05,
                        "shadow":0.2,"warmth":0.0,"bloom":0.4,
                        "lift":0.0,"hdr":0.5,"ambient":0.0,"vignette":0.15},
            "COLD":    {"saturation":1.0,"contrast":1.1,"brightness":0.0,
                        "shadow":0.2,"warmth":-0.4,"bloom":0.15,
                        "lift":0.05,"hdr":0.2,"ambient":0.3,"vignette":0.3},
            "WARM":    {"saturation":1.15,"contrast":1.05,"brightness":0.02,
                        "shadow":0.1,"warmth":0.5,"bloom":0.25,
                        "lift":0.05,"hdr":0.25,"ambient":0.0,"vignette":0.2},
        }
        p = presets.get(name)
        if not p: return
        for k, v in p.items():
            self.sliders[k].setValue(v)
        self._push()
        self._log(f"preset: {name}", "ok")

    def _log(self, msg, level="info"):
        color = {"ok": OK, "err": ERR, "info": DIM}.get(level, DIM)
        self.log.appendHtml(f'<span style="color:{color}">{msg}</span>')

    def _inject(self):
        if self._worker and self._worker.isRunning(): return
        self.btn_inject.setEnabled(False)
        self.log.clear()
        self.lbl_status.setText("INJECTING…")
        target = self.le_target.text().strip() or TARGET
        self._worker = InjectWorker(target)
        self._worker.log.connect(self._log)
        self._worker.finished.connect(self._done)
        self._worker.start()

    def _done(self, ok):
        self.btn_inject.setEnabled(True)
        self.lbl_status.setText("READY" if ok else "FAILED")

    def closeEvent(self, e):
        try: self.params.close()
        except Exception: pass
        super().closeEvent(e)


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("nutella")
    w = Nutella()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()