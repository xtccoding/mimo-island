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
from PySide6.QtWidgets import QApplication, QMainWindow, QMenu, QInputDialog, QMessageBox
from PySide6.QtGui import (
    QColor, QPainter, QPainterPath, QFont, QCursor,
    QLinearGradient, QBrush, QPen, QAction
)

USER_ID = "3210225705"
API_URL = "https://platform.xiaomimimo.com/api/v1/tokenPlan/usage"

COMPACT_W, COMPACT_H = 168, 44
EXPANDED_W, EXPANDED_H = 320, 195

# 预定义颜色常量（避免重复创建）
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

# 缓存字体对象（避免重复创建）
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
    """从各种格式提取 Cookie"""
    import re
    
    # fetch 代码
    match = re.search(r'["\']cookie["\']\s*:\s*["\'](.+?)["\']', text, re.DOTALL)
    if match:
        return match.group(1)
    
    # curl -b 或 --cookie
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
    
    # curl -H 'cookie: ...'
    match = re.search(r"-H\s+['\"]cookie:\s*(.+?)['\"]", text, re.DOTALL)
    if match:
        return match.group(1)
    
    # 纯字符串
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
                
                # 获取月度总用量（最准确）
                month_usage = data.get('monthUsage', {})
                month_items = month_usage.get('items', [])
                month_used = 0
                month_total = 0
                month_percent = 0
                
                for item in month_items:
                    if item.get('name') == 'month_total_token':
                        month_used = item.get('used', 0)
                        month_total = item.get('limit', 0)
                        month_percent = item.get('percent', 0) * 100
                        break
                
                # 获取套餐用量
                usage = data.get('usage', {})
                items = usage.get('items', [])
                
                plan_used = 0
                plan_total = 0
                plan_percent = 0
                compensation_used = 0
                
                for item in items:
                    if item.get('name') == 'plan_total_token':
                        plan_used = item.get('used', 0)
                        plan_total = item.get('limit', 0)
                        plan_percent = item.get('percent', 0) * 100
                    elif item.get('name') == 'compensation_total_token':
                        compensation_used = item.get('used', 0)
                
                # 优先使用月度总用量，如果为0则使用套餐+补偿
                if month_used > 0:
                    used = month_used
                    total = month_total
                    percent = month_percent
                else:
                    used = plan_used + compensation_used
                    total = plan_total
                    percent = (used / total * 100) if total > 0 else 0
                
                self.pm.mark_success()
                self.data_fetched.emit({
                    "used": used,
                    "total": total,
                    "percent": percent,
                    "ok": True
                })
            elif r.status_code == 401:
                self.pm.mark_error("401")
                self.data_fetched.emit({"used": 0, "total": 0, "percent": 0, "ok": False, "msg": "Cookie已过期", "code": 401})
            else:
                self.pm.mark_error(f"{r.status_code}")
                self.data_fetched.emit({"used": 0, "total": 0, "percent": 0, "ok": False, "msg": f"HTTP {r.status_code}"})
        except Exception as e:
            self.pm.mark_error(str(e)[:20])
            self.data_fetched.emit({"used": 0, "total": 0, "percent": 0, "ok": False, "msg": str(e)[:20]})


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

        self.data = {"used": 0, "total": 0, "percent": 0}
        self._pct = SpringValue(0)
        self._used = SpringValue(0)

        self._glow_offset = 0.0
        self._glow_speed = 0.0008
        self._glow_target_speed = 0.0008
        
        self.pm = ProfileManager()

        self.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setMouseTracking(True)

        s = QApplication.primaryScreen().geometry()
        self.move((s.width() - COMPACT_W) // 2, 12)
        self.resize(COMPACT_W, COMPACT_H)

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
            "粘贴 Cookie（支持格式）：\n"
            "• fetch 代码\n"
            "• curl 命令\n"
            "• 纯字符串\n\n"
            "获取：F12 → Network → 右键请求 → Copy as fetch",
            ""
        )
        if ok and text.strip():
            cookie_str = extract_cookie(text.strip())
            name, ok2 = QInputDialog.getText(
                self, "命名配置", "配置名称：",
                text=f"账号{len(self.pm.profiles) + 1}"
            )
            if ok2 and name.strip():
                self.pm.add(name.strip(), cookie_str)
                self.menu.update_profiles()
                self.fetch()

    def delete_current_cookie(self):
        profile = self.pm.get_active()
        if profile:
            reply = QMessageBox.question(
                self, "确认删除",
                f"删除配置 \"{profile.name}\" ？",
                QMessageBox.Yes | QMessageBox.No
            )
            if reply == QMessageBox.Yes:
                self.pm.remove(self.pm.active_index)
                self.menu.update_profiles()
                self.fetch()

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

    def fetch(self):
        if not self.thread.isRunning():
            self.thread.start()

    def on_data(self, d):
        if d["ok"]:
            self.data = d
            self._pct.target(d["percent"])
            self._used.target(d["used"])
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
        # 只在有实际变化时才重绘
        if pct_changed or used_changed or self._hovered or self._error_code == 401:
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
        label_color = QColor(120, 125, 145, alpha)
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
            p.setPen(label_color)
            p.setFont(font_ui(10, QFont.Light))
            p.drawText(QRectF(pad, y0, 50, rh), Qt.AlignVCenter | Qt.AlignLeft, "已用")

            p.setPen(val_color)
            p.setFont(font_mono(11, QFont.Medium))
            p.drawText(QRectF(pad + 52, y0, 200, rh), Qt.AlignVCenter | Qt.AlignLeft, f"{int(self._used.val):,}")

            p.setPen(label_color)
            p.setFont(font_ui(10, QFont.Light))
            p.drawText(QRectF(pad, y0 + rh, 50, rh), Qt.AlignVCenter | Qt.AlignLeft, "总额")

            p.setPen(val_color)
            p.setFont(font_mono(11, QFont.Medium))
            p.drawText(QRectF(pad + 52, y0 + rh, 200, rh), Qt.AlignVCenter | Qt.AlignLeft, f"{self.data.get('total', 0):,}")

            by = y0 + rh * 2 + 12
            bx = float(pad)
            bw = float(w - pad * 2)
            bh = 4.0
            br = 2.0  # 进度条圆角

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

    def leaveEvent(self, e):
        self._hovered = False
        self._glow_target_speed = 0.0008
        self.setCursor(QCursor(Qt.ArrowCursor))

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            self._drag_pos = e.globalPosition().toPoint() - self.pos()
            self._drag_moved = False
        elif e.button() == Qt.RightButton:
            self.menu.update_profiles()
            self.menu.set_pinned(self._pinned)
            self.menu.exec(e.globalPosition().toPoint())

    def mouseMoveEvent(self, e):
        if e.buttons() == Qt.LeftButton:
            np = e.globalPosition().toPoint() - self._drag_pos
            if (np - self.pos()).manhattanLength() > 3:
                self._drag_moved = True
                self.move(np)

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.LeftButton and not self._drag_moved:
            self.toggle()

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
