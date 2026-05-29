import sys
import math
import json
import requests
from pathlib import Path
from datetime import datetime
from PySide6.QtCore import (
    Qt, QPoint, QThread, Signal, QTimer, QPropertyAnimation,
    QEasingCurve, QRect, QRectF, QParallelAnimationGroup, Property
)
from PySide6.QtWidgets import QApplication, QMainWindow, QMenu, QInputDialog, QMessageBox, QLineEdit
from PySide6.QtGui import (
    QColor, QPainter, QPainterPath, QFont, QCursor,
    QLinearGradient, QBrush, QPen, QAction
)

API_URL = "https://platform.xiaomimimo.com/api/v1/tokenPlan/usage"

COMPACT_W, COMPACT_H = 168, 44
EXPANDED_W, EXPANDED_H = 320, 195

# 贴边模式尺寸
DOCKED_H = 6
DOCKED_W = 6
DOCKED_LEN = 200
DOCK_THRESHOLD = 10

class DockMode:
    NONE = 0
    TOP = 1
    BOTTOM = 2
    LEFT = 3
    RIGHT = 4

# 预定义颜色常量
C_TEXT = QColor(235, 237, 245)
C_LABEL = QColor(120, 125, 145)
C_DIM = QColor(80, 85, 100)
C_ACCENT = QColor(70, 140, 255)
C_ERROR = QColor(255, 100, 100)
C_BG_0 = QColor(14, 14, 20, 220)
C_BG_1 = QColor(18, 18, 26, 217)
C_GLOW_HOVER = QColor(70, 140, 255)
C_GLOW_ERROR = QColor(255, 80, 80)

CONFIG_DIR = Path.home() / ".mimo_monitor"
PROFILES_FILE = CONFIG_DIR / "profiles.json"
STATE_FILE = CONFIG_DIR / "state.json"

_font_cache = {}

def _get_font(key, size, weight):
    if key not in _font_cache:
        f = QFont()
        if key.startswith('mono'):
            f.setFamilies(["JetBrains Mono", "Cascadia Code", "Consolas", "SF Mono"])
        else:
            f.setFamilies(["Segoe UI Variable Display", "Microsoft YaHei UI", "PingFang SC", "SF Pro Display"])
        f.setPixelSize(size)
        f.setWeight(weight)
        f.setHintingPreference(QFont.PreferNoHinting)
        _font_cache[key] = f
    return _font_cache[key]

def font_ui(size=10, weight=QFont.Normal):
    return _get_font(f'ui_{size}_{weight.value}', size, weight)

def font_mono(size=11, weight=QFont.Medium):
    return _get_font(f'mono_{size}_{weight.value}', size, weight)


class CookieProfile:
    def __init__(self, name, cookie_str, created_at=None):
        self.name = name
        self.cookie_str = cookie_str
        self.created_at = created_at or datetime.now().isoformat()
        self.last_used = None
        self.last_error = None
    
    def to_dict(self):
        return {
            "name": self.name,
            "cookie_str": self.cookie_str,
            "created_at": self.created_at,
            "last_used": self.last_used,
            "last_error": self.last_error
        }
    
    @classmethod
    def from_dict(cls, d):
        p = cls(d["name"], d["cookie_str"], d.get("created_at"))
        p.last_used = d.get("last_used")
        p.last_error = d.get("last_error")
        return p


class ProfileManager:
    def __init__(self):
        self.profiles = []
        self.active_index = 0
        self.load()
    
    def load(self):
        if PROFILES_FILE.exists():
            try:
                data = json.loads(PROFILES_FILE.read_text(encoding='utf-8'))
                self.profiles = [CookieProfile.from_dict(p) for p in data.get("profiles", [])]
                self.active_index = data.get("active_index", 0)
            except:
                self.profiles = []
                self.active_index = 0
    
    def save(self):
        CONFIG_DIR.mkdir(exist_ok=True)
        data = {
            "profiles": [p.to_dict() for p in self.profiles],
            "active_index": self.active_index
        }
        PROFILES_FILE.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')
    
    def add(self, name, cookie_str):
        profile = CookieProfile(name, cookie_str)
        self.profiles.append(profile)
        self.active_index = len(self.profiles) - 1
        self.save()
        return profile
    
    def remove(self, index):
        if 0 <= index < len(self.profiles):
            self.profiles.pop(index)
            if self.active_index >= len(self.profiles):
                self.active_index = max(0, len(self.profiles) - 1)
            self.save()
    
    def rename(self, index, new_name):
        if 0 <= index < len(self.profiles):
            self.profiles[index].name = new_name
            self.save()
    
    def get_active(self):
        if self.profiles and 0 <= self.active_index < len(self.profiles):
            return self.profiles[self.active_index]
        return None
    
    def set_active(self, index):
        if 0 <= index < len(self.profiles):
            self.active_index = index
            self.save()
    
    def mark_error(self, error_msg):
        profile = self.get_active()
        if profile:
            profile.last_error = error_msg
            self.save()
    
    def mark_success(self):
        profile = self.get_active()
        if profile:
            profile.last_used = datetime.now().isoformat()
            profile.last_error = None
            self.save()


def extract_cookie(text):
    import re
    match = re.search(r'["\']cookie["\']\s*:\s*["\'](.+?)["\']', text, re.DOTALL)
    if match:
        return match.group(1)
    if "-b '" in text or "--cookie '" in text:
        start = text.find("-b '")
        if start == -1:
            start = text.find("--cookie '")
        if start != -1:
            quote_start = text.find("'", start)
            if quote_start != -1:
                quote_end = quote_start + 1
                while quote_end < len(text):
                    if text[quote_end] == "'" and text[quote_end - 1] != "\\":
                        break
                    quote_end += 1
                if quote_end < len(text):
                    return text[quote_start + 1:quote_end]
    match = re.search(r"-H\s+['\"]cookie:\s*(.+?)['\"]", text, re.DOTALL)
    if match:
        return match.group(1)
    if '=' in text and ';' in text:
        return text.strip()
    return text.strip()


class FetchThread(QThread):
    data_fetched = Signal(dict)
    
    def __init__(self, profile_manager):
        super().__init__()
        self.pm = profile_manager
    
    def run(self):
        profile = self.pm.get_active()
        if not profile:
            self.data_fetched.emit({"used": 0, "total": 0, "percent": 0, "ok": False, "msg": "请添加Cookie"})
            return
        try:
            headers = {
                'accept': '*/*',
                'accept-language': 'zh',
                'content-type': 'application/json',
                'x-timezone': 'Asia/Shanghai',
                'user-agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36',
                'referer': 'https://platform.xiaomimimo.com/console/plan-manage',
                'cookie': profile.cookie_str
            }
            r = requests.get(API_URL, headers=headers, timeout=10)
            if r.status_code == 200:
                res = r.json()
                data = res.get('data', {})
                month_usage = data.get('monthUsage', {})
                month_items = month_usage.get('items', [])
                month_used = month_total = month_percent = 0
                for item in month_items:
                    if item.get('name') == 'month_total_token':
                        month_used = item.get('used', 0)
                        month_total = item.get('limit', 0)
                        month_percent = item.get('percent', 0) * 100
                        break
                usage = data.get('usage', {})
                items = usage.get('items', [])
                plan_used = plan_total = plan_percent = 0
                comp_used = comp_total = comp_percent = 0
                for item in items:
                    if item.get('name') == 'plan_total_token':
                        plan_used = item.get('used', 0)
                        plan_total = item.get('limit', 0)
                        plan_percent = item.get('percent', 0) * 100
                    elif item.get('name') == 'compensation_total_token':
                        comp_used = item.get('used', 0)
                        comp_total = item.get('limit', 0)
                        comp_percent = item.get('percent', 0) * 100
                self.pm.mark_success()
                self.data_fetched.emit({
                    "plan_used": plan_used, "plan_total": plan_total, "plan_percent": plan_percent,
                    "comp_used": comp_used, "comp_total": comp_total, "comp_percent": comp_percent,
                    "month_used": month_used, "month_total": month_total, "month_percent": month_percent,
                    "ok": True
                })
            elif r.status_code == 401:
                self.pm.mark_error("401")
                self.data_fetched.emit({"ok": False, "msg": "Cookie已过期", "code": 401})
            else:
                self.pm.mark_error(f"{r.status_code}")
                self.data_fetched.emit({"ok": False, "msg": f"HTTP {r.status_code}"})
        except Exception as e:
            self.pm.mark_error(str(e)[:20])
            self.data_fetched.emit({"ok": False, "msg": str(e)[:20]})


class SpringValue:
    def __init__(self, v=0):
        self._cur = v
        self._tgt = v
        self._vel = 0

    def target(self, t):
        self._tgt = t

    def tick(self, dt=0.016, spring=160, damp=10):
        d = self._tgt - self._cur
        self._vel += d * spring * dt
        self._vel *= max(0, 1 - damp * dt)
        self._cur += self._vel * dt
        return abs(d) > 0.0005 or abs(self._vel) > 0.005

    @property
    def val(self):
        return self._cur


class ContextMenu(QMenu):
    def __init__(self, parent):
        super().__init__(parent)
        self.parent = parent
        self.setStyleSheet("""
            QMenu {
                background: rgba(16, 16, 22, 245);
                border: 1px solid rgba(70, 140, 255, 50);
                border-radius: 10px;
                padding: 6px;
                min-width: 200px;
            }
            QMenu::item {
                color: rgba(210, 215, 230, 220);
                padding: 8px 20px;
                font-size: 12px;
                border-radius: 6px;
                margin: 2px 4px;
            }
            QMenu::item:selected {
                background: rgba(70, 140, 255, 35);
            }
            QMenu::item:checked {
                color: rgba(100, 200, 255, 255);
            }
            QMenu::separator {
                height: 1px;
                background: rgba(255,255,255,18);
                margin: 4px 10px;
            }
        """)
        self.setFont(font_ui(11))
        self._build_menu()
    
    def _build_menu(self):
        self.clear()
        self.profile_menu = QMenu("切换账号", self)
        self.profile_menu.setStyleSheet(self.styleSheet())
        self.addMenu(self.profile_menu)
        a_add = QAction("添加Cookie", self)
        a_add.triggered.connect(self.parent.add_cookie)
        self.addAction(a_add)
        a_rename = QAction("重命名当前", self)
        a_rename.triggered.connect(self.parent.rename_current_cookie)
        self.addAction(a_rename)
        a_del = QAction("删除当前", self)
        a_del.triggered.connect(self.parent.delete_current_cookie)
        self.addAction(a_del)
        self.addSeparator()
        a1 = QAction("刷新数据", self)
        a1.triggered.connect(self.parent.fetch)
        self.addAction(a1)
        self.addSeparator()
        self._pin = QAction("取消置顶" if self.parent._pinned else "置顶窗口", self)
        self._pin.triggered.connect(self.parent.toggle_top)
        self.addAction(self._pin)
        self.addSeparator()
        a3 = QAction("退出程序", self)
        a3.triggered.connect(QApplication.quit)
        self.addAction(a3)
    
    def update_profiles(self):
        self.profile_menu.clear()
        pm = self.parent.pm
        for i, profile in enumerate(pm.profiles):
            name = profile.name
            if profile.last_error:
                name += " ⚠"
            action = QAction(name, self)
            action.setCheckable(True)
            action.setChecked(i == pm.active_index)
            action.triggered.connect(lambda checked, idx=i: self.parent.switch_profile(idx))
            self.profile_menu.addAction(action)
        if not pm.profiles:
            empty = QAction("(无配置)", self)
            empty.setEnabled(False)
            self.profile_menu.addAction(empty)
    
    def set_pinned(self, on):
        self._pin.setText("取消置顶" if on else "置顶窗口")


class DynamicIsland(QMainWindow):
    def __init__(self):
        super().__init__()
        self._expanded = False
        self._hovered = False
        self._pinned = True
        self._drag_pos = QPoint()
        self._drag_moved = False
        self._phase = 0.0
        self._item_opacity = 1.0
        self._error_msg = ""
        self._error_code = 0

        self._dock_mode = DockMode.NONE
        self._docked = False
        self._dock_hovered = False
        self._dock_opacity = 0.0
        self._undocked_from_dock = False
        self._mouse_pressed = False
        self._last_dock_mode = DockMode.TOP
        self._menu_open = False
        self._animating = False
        self._re_dock_timer = QTimer(self)
        self._re_dock_timer.setSingleShot(True)
        self._re_dock_timer.timeout.connect(self._re_dock_check)

        self.data = {
            "plan_used": 0, "plan_total": 0, "plan_percent": 0,
            "comp_used": 0, "comp_total": 0, "comp_percent": 0,
            "month_used": 0, "month_total": 0, "month_percent": 0,
        }
        self._pct = SpringValue(0)
        self._used = SpringValue(0)

        self._glow_offset = 0.0
        self._glow_speed = 0.0008
        self._glow_target_speed = 0.0008
        
        self.pm = ProfileManager()

        self.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setMouseTracking(True)

        self._load_state()

        self.menu = ContextMenu(self)

        self.thread = FetchThread(self.pm)
        self.thread.data_fetched.connect(self.on_data)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.fetch)
        self.timer.start(300000)
        self.fetch()

        self._frame = QTimer(self)
        self._frame.timeout.connect(self._tick)
        self._frame.start(16)

        self._glow_anim = QPropertyAnimation(self, b"glowOffset")
        self._glow_anim.setDuration(2800)
        self._glow_anim.setStartValue(0.0)
        self._glow_anim.setEndValue(1.0)
        self._glow_anim.setLoopCount(-1)
        self._glow_anim.setEasingCurve(QEasingCurve.Linear)
        self._glow_anim.start()

    def add_cookie(self):
        text, ok = QInputDialog.getMultiLineText(
            self, "添加Cookie", 
            "粘贴 Cookie（支持格式）：\n• fetch 代码\n• curl 命令\n• 纯字符串\n\n获取：F12 → Network → 右键请求 → Copy as fetch", ""
        )
        if ok and text.strip():
            cookie_str = extract_cookie(text.strip())
            name, ok2 = QInputDialog.getText(self, "命名配置", "配置名称：", text=f"账号{len(self.pm.profiles) + 1}")
            if ok2 and name.strip():
                self.pm.add(name.strip(), cookie_str)
                self.menu.update_profiles()
                self.fetch()

    def _load_state(self):
        if STATE_FILE.exists():
            try:
                data = json.loads(STATE_FILE.read_text(encoding='utf-8'))
                dock_mode = data.get("dock_mode", 0)
                x = data.get("x", -1)
                y = data.get("y", -1)
                if dock_mode > 0 and x >= 0 and y >= 0:
                    self._dock_mode = dock_mode
                    self._last_dock_mode = dock_mode
                    self._docked = True
                    self._dock_opacity = 1.0
                    self.resize(DOCKED_LEN, DOCKED_H) if dock_mode in (DockMode.TOP, DockMode.BOTTOM) else self.resize(DOCKED_W, DOCKED_LEN)
                    self.move(x, y)
                    return
            except:
                pass
        s = QApplication.primaryScreen().availableGeometry()
        self.move((s.width() - COMPACT_W) // 2, s.top() + 12)
        self.resize(COMPACT_W, COMPACT_H)

    def _save_state(self):
        CONFIG_DIR.mkdir(exist_ok=True)
        pos = self.pos()
        data = {
            "dock_mode": self._dock_mode if self._docked else 0,
            "x": pos.x(),
            "y": pos.y()
        }
        STATE_FILE.write_text(json.dumps(data), encoding='utf-8')

    def closeEvent(self, e):
        self._save_state()
        super().closeEvent(e)

    def delete_current_cookie(self):
        profile = self.pm.get_active()
        if profile:
            reply = QMessageBox.question(self, "确认删除", f"删除配置 \"{profile.name}\" ？", QMessageBox.Yes | QMessageBox.No)
            if reply == QMessageBox.Yes:
                self.pm.remove(self.pm.active_index)
                self.menu.update_profiles()
                self.fetch()

    def rename_current_cookie(self):
        profile = self.pm.get_active()
        if profile:
            name, ok = QInputDialog.getText(self, "重命名", "新名称：", QLineEdit.Normal, profile.name)
            if ok and name.strip():
                self.pm.rename(self.pm.active_index, name.strip())
                self.menu.update_profiles()

    def switch_profile(self, index):
        self.pm.set_active(index)
        self._error_msg = ""
        self._error_code = 0
        self.menu.update_profiles()
        self.fetch()

    def get_glow_offset(self):
        return self._glow_offset

    def set_glow_offset(self, v):
        self._glow_offset = v
        self.update()

    glowOffset = Property(float, get_glow_offset, set_glow_offset)

    def get_dock_opacity(self):
        return self._dock_opacity

    def set_dock_opacity(self, v):
        self._dock_opacity = v
        self.update()

    dockOpacity = Property(float, get_dock_opacity, set_dock_opacity)

    def fetch(self):
        if not self.thread.isRunning():
            self.thread.start()

    def on_data(self, d):
        if d["ok"]:
            self.data = d
            if d.get("comp_used", 0) > 0:
                self._pct.target(d["comp_percent"])
                self._used.target(d["comp_used"])
            else:
                self._pct.target(d["plan_percent"])
                self._used.target(d["plan_used"])
            self._error_msg = ""
            self._error_code = 0
        else:
            self._error_msg = d.get("msg", "获取失败")
            self._error_code = d.get("code", 0)

    def _tick(self):
        self._phase += 0.04
        pct_changed = self._pct.tick()
        used_changed = self._used.tick()
        self._glow_speed += (self._glow_target_speed - self._glow_speed) * 0.1
        if pct_changed or used_changed or self._hovered or self._error_code == 401 or self._docked:
            self.update()

    def toggle_top(self):
        self._pinned = not self._pinned
        f = self.windowFlags()
        f = (f | Qt.WindowStaysOnTopHint) if self._pinned else (f & ~Qt.WindowStaysOnTopHint)
        self.setWindowFlags(f)
        self.show()
        self.menu.set_pinned(self._pinned)

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()

        if self._docked:
            self._draw_docked(p, w, h)
        else:
            path = QPainterPath()
            r = 12 if self._expanded else min(h / 2, 22)
            path.addRoundedRect(QRectF(0, 0, w, h), r, r)
            p.setClipPath(path, Qt.IntersectClip)
            self._draw_bg(p, w, h)
            self._draw_glow(p, w, h)
            self._draw_border(p, w, h)
            if not self._expanded:
                self._draw_compact(p, w, h)
            else:
                self._draw_expanded(p, w, h)
        p.end()

    def _draw_docked(self, p, w, h):
        pct = self._pct.val
        a = self._dock_opacity
        if a < 0.01:
            return

        is_vert = self._dock_mode in (DockMode.LEFT, DockMode.RIGHT)
        
        r = min(h / 2, 3) if not is_vert else min(w / 2, 3)
        path = QPainterPath()
        path.addRoundedRect(QRectF(0, 0, w, h), r, r)
        p.setClipPath(path, Qt.IntersectClip)

        bg = QLinearGradient(0, 0, w, h)
        bg.setColorAt(0, QColor(14, 14, 20, int(200 * a)))
        bg.setColorAt(1, QColor(18, 18, 26, int(200 * a)))
        p.setPen(Qt.NoPen)
        p.setBrush(bg)
        p.drawRoundedRect(QRectF(0, 0, w, h), r, r)

        glow_a = 0.15 + 0.05 * math.sin(self._phase)
        g = QColor(70, 140, 255, int(255 * glow_a * a))
        p.setBrush(g)
        p.drawRoundedRect(QRectF(-1, -1, w + 2, h + 2), r + 1, r + 1)

        if not is_vert:
            fw = (w - 4) * min(pct / 100, 1)
            if fw > 1:
                fg = QLinearGradient(2, 0, 2 + fw, 0)
                fg.setColorAt(0, QColor(59, 130, 246, int(255 * a)))
                fg.setColorAt(1, QColor(34, 211, 238, int(255 * a)))
                p.setBrush(fg)
                p.drawRoundedRect(QRectF(2, 2, fw, h - 4), r, r)
        else:
            fh = (h - 4) * min(pct / 100, 1)
            if fh > 1:
                fg = QLinearGradient(0, h - 2 - fh, 0, h - 2)
                fg.setColorAt(0, QColor(59, 130, 246, int(255 * a)))
                fg.setColorAt(1, QColor(34, 211, 238, int(255 * a)))
                p.setBrush(fg)
                p.drawRoundedRect(QRectF(2, h - 2 - fh, w - 4, fh), r, r)

    def _draw_bg(self, p, w, h):
        grad = QLinearGradient(0, 0, w, h)
        grad.setColorAt(0, QColor(14, 14, 20, 220))
        grad.setColorAt(0.5, QColor(18, 18, 26, 217))
        grad.setColorAt(1, QColor(14, 14, 20, 220))
        p.setPen(Qt.NoPen)
        p.setBrush(grad)
        r = 12 if self._expanded else min(h / 2, 22)
        p.drawRoundedRect(QRectF(0, 0, w, h), r, r)

    def _draw_glow(self, p, w, h):
        if self._error_code == 401:
            glow_a = 0.15 + 0.1 * math.sin(self._phase * 2)
            g = QColor(255, 80, 80, int(255 * glow_a))
        else:
            glow_a = 0.12 + 0.05 * math.sin(self._phase)
            if self._hovered:
                glow_a += 0.08
            g = QColor(70, 140, 255, int(255 * glow_a))
        p.setPen(Qt.NoPen)
        p.setBrush(g)
        r = 14 if self._expanded else min(h / 2, 22) + 2
        p.drawRoundedRect(QRectF(-1.5, -1.5, w + 3, h + 3), r, r)

    def _draw_border(self, p, w, h):
        path = QPainterPath()
        r = 12 if self._expanded else min(h / 2, 22)
        path.addRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), r, r)
        if self._error_code == 401:
            ba = 40 + int(20 * math.sin(self._phase * 2))
            bg = QLinearGradient(0, 0, w, 0)
            bg.setColorAt(0, QColor(255, 80, 80, ba))
            bg.setColorAt(0.5, QColor(255, 120, 80, ba))
            bg.setColorAt(1, QColor(255, 80, 80, ba))
        else:
            ba = 22 if not self._hovered else 45
            bg = QLinearGradient(0, 0, w, 0)
            bg.setColorAt(0, QColor(70, 140, 255, ba))
            bg.setColorAt(0.33, QColor(100, 200, 255, ba))
            bg.setColorAt(0.66, QColor(140, 100, 255, ba))
            bg.setColorAt(1, QColor(70, 140, 255, ba))
        p.setPen(QPen(QBrush(bg), 1.2))
        p.setBrush(Qt.NoBrush)
        p.drawPath(path)

    def _draw_flow_light(self, p, w, h):
        path = QPainterPath()
        r = 12 if self._expanded else min(h / 2, 22)
        path.addRoundedRect(QRectF(0, 0, w, h), r, r)
        offset = self._glow_offset
        cx = w * offset
        band_w = w * 0.4
        if self._error_code == 401:
            g = QLinearGradient(cx - band_w, 0, cx + band_w, 0)
            g.setColorAt(0.0, QColor(255, 255, 255, 0))
            g.setColorAt(0.5, QColor(255, 100, 100, 15))
            g.setColorAt(1.0, QColor(255, 255, 255, 0))
        else:
            g = QLinearGradient(cx - band_w, 0, cx + band_w, 0)
            g.setColorAt(0.0, QColor(255, 255, 255, 0))
            g.setColorAt(0.3, QColor(100, 200, 255, 8))
            g.setColorAt(0.5, QColor(100, 200, 255, 18))
            g.setColorAt(0.7, QColor(100, 200, 255, 8))
            g.setColorAt(1.0, QColor(255, 255, 255, 0))
        p.setPen(Qt.NoPen)
        p.setBrush(g)
        p.drawPath(path)

    def _draw_compact(self, p, w, h):
        self._draw_flow_light(p, w, h)
        pct = self._pct.val
        cy = h / 2
        ix = 30
        isz = 8
        p.setPen(Qt.NoPen)
        if self._error_code == 401:
            ig = QLinearGradient(ix - 5, cy - 5, ix + 5, cy + 5)
            ig.setColorAt(0, QColor(255, 100, 100))
            ig.setColorAt(1, QColor(255, 60, 60))
        else:
            ig = QLinearGradient(ix - 5, cy - 5, ix + 5, cy + 5)
            ig.setColorAt(0, QColor(100, 200, 255))
            ig.setColorAt(1, C_ACCENT)
        p.setBrush(ig)
        p.drawEllipse(int(ix - isz / 2), int(cy - isz / 2), isz, isz)
        if self._error_msg:
            p.setPen(C_ERROR if self._error_code == 401 else QColor(255, 200, 100))
            p.setFont(font_ui(9))
            p.drawText(QRectF(46, 0, 100, h), Qt.AlignVCenter | Qt.AlignLeft, self._error_msg)
        else:
            p.setPen(C_TEXT)
            p.setFont(font_mono(15, QFont.DemiBold))
            p.drawText(QRectF(46, 0, 55, h), Qt.AlignVCenter | Qt.AlignLeft, f"{pct:.1f}")
            p.setPen(C_DIM)
            p.setFont(font_ui(10))
            p.drawText(QRectF(108, 0, 30, h), Qt.AlignVCenter | Qt.AlignLeft, "%")

    def _draw_expanded(self, p, w, h):
        self._draw_flow_light(p, w, h)
        pad = 28
        a = self._item_opacity
        if a < 0.01:
            return
        alpha = int(255 * a)
        title_color = QColor(235, 237, 245, alpha)
        val_color = QColor(240, 242, 250, alpha)
        dim_color = QColor(80, 85, 100, alpha)
        error_color = QColor(255, 100, 100, alpha)
        p.setPen(title_color)
        p.setFont(font_ui(13, QFont.DemiBold))
        p.drawText(QRectF(pad, 22, 200, 24), Qt.AlignVCenter | Qt.AlignLeft, "MiMo Token Monitor")
        profile = self.pm.get_active()
        if profile:
            profile_text = profile.name
            if profile.last_error:
                profile_text += " ⚠"
            p.setPen(QColor(100, 200, 255, int(alpha * 0.7)))
            p.setFont(font_ui(9))
            p.drawText(QRectF(pad, 42, 200, 16), Qt.AlignVCenter | Qt.AlignLeft, profile_text)
        y0 = 66
        rh = 24
        if self._error_msg:
            if self._error_code == 401:
                p.setPen(error_color)
                p.setFont(font_ui(11))
                p.drawText(QRectF(pad, y0, w - pad * 2, rh), Qt.AlignCenter, "Cookie 已过期")
                p.setPen(dim_color)
                p.setFont(font_ui(9))
                p.drawText(QRectF(pad, y0 + rh + 4, w - pad * 2, rh), Qt.AlignCenter, "右键 → 添加新Cookie")
            else:
                p.setPen(QColor(255, 200, 100, alpha))
                p.setFont(font_ui(11))
                p.drawText(QRectF(pad, y0, w - pad * 2, rh * 2), Qt.AlignCenter, self._error_msg)
        else:
            plan_used = self.data.get('plan_used', 0)
            plan_total = self.data.get('plan_total', 0)
            comp_used = self.data.get('comp_used', 0)
            comp_total = self.data.get('comp_total', 0)
            if comp_total > 0:
                p.setPen(QColor(100, 255, 180, alpha))
                p.setFont(font_ui(9, QFont.Light))
                p.drawText(QRectF(pad, y0, 60, rh), Qt.AlignVCenter | Qt.AlignLeft, "补偿")
                p.setPen(val_color)
                p.setFont(font_mono(10, QFont.Medium))
                p.drawText(QRectF(pad + 60, y0, 180, rh), Qt.AlignVCenter | Qt.AlignLeft, f"{comp_used:,} / {comp_total:,}")
                y0 += rh
            if plan_total > 0:
                p.setPen(QColor(100, 200, 255, alpha))
                p.setFont(font_ui(9, QFont.Light))
                p.drawText(QRectF(pad, y0, 60, rh), Qt.AlignVCenter | Qt.AlignLeft, "套餐")
                p.setPen(val_color)
                p.setFont(font_mono(10, QFont.Medium))
                p.drawText(QRectF(pad + 60, y0, 180, rh), Qt.AlignVCenter | Qt.AlignLeft, f"{plan_used:,} / {plan_total:,}")
                y0 += rh
            by = y0 + 8
            bx = float(pad)
            bw = float(w - pad * 2)
            bh = 4.0
            br = 2.0
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(255, 255, 255, 12))
            p.drawRoundedRect(QRectF(bx, by, bw, bh), br, br)
            pct = self._pct.val
            fw = bw * min(pct / 100, 1)
            if fw > 1:
                fg = QLinearGradient(bx, 0, bx + fw, 0)
                fg.setColorAt(0, QColor(59, 130, 246, alpha))
                fg.setColorAt(1, QColor(34, 211, 238, alpha))
                p.setBrush(fg)
                p.drawRoundedRect(QRectF(bx, by, fw, bh), br, br)
            p.setPen(QColor(200, 205, 220, alpha))
            p.setFont(font_mono(10, QFont.Medium))
            p.drawText(QRectF(pad, by + bh + 8, 80, 18), Qt.AlignVCenter | Qt.AlignLeft, f"{pct:.2f}%")
        p.setPen(dim_color)
        p.setFont(font_ui(9, QFont.Light))
        p.drawText(QRectF(pad, h - 24, w - pad * 2, 14), Qt.AlignCenter, "右键菜单 · 左键收起")

    def enterEvent(self, e):
        self._hovered = True
        self._glow_target_speed = 0.002
        self.setCursor(QCursor(Qt.PointingHandCursor))
        if self._docked and not self._dock_hovered and not self._animating:
            self._dock_hovered = True
            self._undocked_from_dock = True
            self._animating = True
            self._undock()

    def leaveEvent(self, e):
        self._hovered = False
        self._glow_target_speed = 0.0008
        self.setCursor(QCursor(Qt.ArrowCursor))
        if self._undocked_from_dock and not self._mouse_pressed and not self._menu_open:
            self._animating = True
            self._re_dock_timer.start(400)
        self._dock_hovered = False

    def _re_dock_check(self):
        if self._undocked_from_dock and not self._mouse_pressed and not self._hovered:
            self._dock(self._last_dock_mode)
        self._animating = False

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            self._re_dock_timer.stop()
            self._animating = False
            if self._docked:
                return
            self._mouse_pressed = True
            self._undocked_from_dock = False
            self._drag_pos = e.globalPosition().toPoint() - self.pos()
            self._drag_moved = False
        elif e.button() == Qt.RightButton:
            if self._docked:
                return
            self._menu_open = True
            self.menu.update_profiles()
            self.menu.set_pinned(self._pinned)
            self.menu.exec(e.globalPosition().toPoint())
            self._menu_open = False

    def mouseMoveEvent(self, e):
        if e.buttons() == Qt.LeftButton and not self._docked:
            np = e.globalPosition().toPoint() - self._drag_pos
            if (np - self.pos()).manhattanLength() > 3:
                self._drag_moved = True
                self.move(np)

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.LeftButton:
            self._mouse_pressed = False
            if self._docked:
                return
            if not self._drag_moved:
                self.toggle()
            else:
                self._check_dock()
                self._undocked_from_dock = False

    def _check_dock_during_drag(self):
        if self._expanded or self._docked:
            return
        screen = QApplication.primaryScreen().availableGeometry()
        pos = self.pos()
        w, h = self.width(), self.height()
        dock_mode = DockMode.NONE
        if pos.y() <= screen.top() + DOCK_THRESHOLD:
            dock_mode = DockMode.TOP
        elif pos.y() + h >= screen.bottom() - DOCK_THRESHOLD:
            dock_mode = DockMode.BOTTOM
        elif pos.x() <= screen.left() + DOCK_THRESHOLD:
            dock_mode = DockMode.LEFT
        elif pos.x() + w >= screen.right() - DOCK_THRESHOLD:
            dock_mode = DockMode.RIGHT
        if dock_mode != DockMode.NONE:
            self._dock(dock_mode)

    def _check_dock(self):
        if self._expanded:
            return
        screen = QApplication.primaryScreen().availableGeometry()
        pos = self.pos()
        w, h = self.width(), self.height()
        dock_mode = DockMode.NONE
        if pos.y() <= screen.top() + DOCK_THRESHOLD:
            dock_mode = DockMode.TOP
        elif pos.y() + h >= screen.bottom() - DOCK_THRESHOLD:
            dock_mode = DockMode.BOTTOM
        elif pos.x() <= screen.left() + DOCK_THRESHOLD:
            dock_mode = DockMode.LEFT
        elif pos.x() + w >= screen.right() - DOCK_THRESHOLD:
            dock_mode = DockMode.RIGHT
        if dock_mode != DockMode.NONE:
            self._dock(dock_mode)

    def _dock(self, mode):
        self._docked = True
        self._dock_mode = mode
        self._last_dock_mode = mode
        self._expanded = False
        screen = QApplication.primaryScreen().availableGeometry()
        cur = self.geometry()
        cx = cur.center().x()
        cy = cur.center().y()
        
        visible = 146
        
        if mode == DockMode.TOP:
            target = QRect(cx - DOCKED_LEN // 2, screen.top(), DOCKED_LEN, DOCKED_H)
        elif mode == DockMode.BOTTOM:
            target = QRect(cx - DOCKED_LEN // 2, screen.bottom() - DOCKED_H, DOCKED_LEN, DOCKED_H)
        elif mode == DockMode.LEFT:
            target = QRect(screen.left(), cy - DOCKED_LEN // 2, DOCKED_W, DOCKED_LEN)
        elif mode == DockMode.RIGHT:
            target = QRect(screen.right() - DOCKED_W, cy - DOCKED_LEN // 2, DOCKED_W, DOCKED_LEN)

        self.setGeometry(target)
        self._dock_opacity = 1.0
        self._save_state()

    def _undock(self):
        self._docked = False
        self._dock_hovered = False
        self._undocked_from_dock = True
        cur = self.geometry()
        screen = QApplication.primaryScreen().availableGeometry()
        
        visible = 146
        
        nx = cur.center().x() - COMPACT_W // 2
        ny = cur.center().y() - COMPACT_H // 2
        
        if self._last_dock_mode == DockMode.TOP:
            ny = screen.top()
        elif self._last_dock_mode == DockMode.BOTTOM:
            ny = screen.bottom() - COMPACT_H
        elif self._last_dock_mode == DockMode.LEFT:
            nx = screen.left() - (COMPACT_W - visible)
        elif self._last_dock_mode == DockMode.RIGHT:
            nx = screen.right() - visible
        
        nx = max(screen.left() - (COMPACT_W - visible), min(nx, screen.right() - visible))
        ny = max(screen.top(), min(ny, screen.bottom() - COMPACT_H))
        
        target = QRect(nx, ny, COMPACT_W, COMPACT_H)

        self._undock_anim = QParallelAnimationGroup(self)
        geo = QPropertyAnimation(self, b"geometry")
        geo.setDuration(350)
        geo.setEasingCurve(QEasingCurve.OutExpo)
        geo.setStartValue(cur)
        geo.setEndValue(target)
        self._undock_anim.addAnimation(geo)
        opac = QPropertyAnimation(self, b"dockOpacity")
        opac.setDuration(250)
        opac.setStartValue(1.0)
        opac.setEndValue(0.0)
        opac.setEasingCurve(QEasingCurve.OutCubic)
        self._undock_anim.addAnimation(opac)
        self._undock_anim.start()
        self._save_state()

    def toggle(self):
        tw = EXPANDED_W if not self._expanded else COMPACT_W
        th = EXPANDED_H if not self._expanded else COMPACT_H
        self._expanded = not self._expanded
        self._grp = QParallelAnimationGroup(self)
        geo = QPropertyAnimation(self, b"geometry")
        geo.setDuration(420)
        geo.setEasingCurve(QEasingCurve.OutExpo)
        cur = self.geometry()
        nx = cur.center().x() - tw // 2
        geo.setStartValue(cur)
        geo.setEndValue(QRect(nx, cur.top(), tw, th))
        self._grp.addAnimation(geo)
        if self._expanded:
            self._item_opacity = 0.0
            QTimer.singleShot(100, self._fade_in)
        else:
            self._item_opacity = 1.0
            opac = QPropertyAnimation(self, b"itemOpacity")
            opac.setDuration(100)
            opac.setStartValue(1.0)
            opac.setEndValue(0.0)
            opac.setEasingCurve(QEasingCurve.InCubic)
            self._grp.addAnimation(opac)
        self._grp.start()

    def _fade_in(self):
        opac = QPropertyAnimation(self, b"itemOpacity")
        opac.setDuration(280)
        opac.setStartValue(0.0)
        opac.setEndValue(1.0)
        opac.setEasingCurve(QEasingCurve.OutCubic)
        opac.start()
        self._fade_anim = opac

    def get_item_opacity(self):
        return self._item_opacity

    def set_item_opacity(self, v):
        self._item_opacity = v
        self.update()

    itemOpacity = Property(float, get_item_opacity, set_item_opacity)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)
    win = DynamicIsland()
    win.show()
    sys.exit(app.exec())
