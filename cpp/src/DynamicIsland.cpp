#include "DynamicIsland.h"
#include <QRegularExpression>
#include <QLineEdit>

SpringValue::SpringValue(double v) : m_cur(v), m_tgt(v), m_vel(0) {}
void SpringValue::target(double t) { m_tgt = t; }
bool SpringValue::tick(double dt, double spring, double damp) {
    double d = m_tgt - m_cur;
    m_vel += d * spring * dt;
    m_vel *= qMax(0.0, 1 - damp * dt);
    m_cur += m_vel * dt;
    return std::abs(d) > 0.0005 || std::abs(m_vel) > 0.005;
}

ContextMenu::ContextMenu(QWidget* parent) : QMenu(parent) {
    setStyleSheet(R"(
        QMenu { background: rgba(16,16,22,245); border: 1px solid rgba(70,140,255,50); border-radius: 10px; padding: 6px; min-width: 200px; }
        QMenu::item { color: rgba(210,215,230,220); padding: 8px 20px; font-size: 12px; border-radius: 6px; margin: 2px 4px; }
        QMenu::item:selected { background: rgba(70,140,255,35); }
        QMenu::item:checked { color: rgba(100,200,255,255); }
        QMenu::separator { height: 1px; background: rgba(255,255,255,18); margin: 4px 10px; }
    )");
    buildMenu();
}

void ContextMenu::buildMenu() {
    clear();
    m_profileMenu = addMenu("切换账号");
    m_profileMenu->setStyleSheet(styleSheet());
    DynamicIsland* island = qobject_cast<DynamicIsland*>(parent());
    QMenu::addAction("添加Cookie", island, &DynamicIsland::addCookie);
    QMenu::addAction("重命名当前", island, &DynamicIsland::renameCurrentCookie);
    QMenu::addAction("删除当前", island, &DynamicIsland::deleteCurrentCookie);
    addSeparator();
    QMenu::addAction("刷新数据", island, &DynamicIsland::fetch);
    addSeparator();
    m_pinAction = QMenu::addAction("置顶窗口");
    connect(m_pinAction, &QAction::triggered, island, &DynamicIsland::toggleTop);
    addSeparator();
    QMenu::addAction("退出程序", qApp, &QApplication::quit);
}

void ContextMenu::updateProfiles() {
    m_profileMenu->clear();
    DynamicIsland* island = qobject_cast<DynamicIsland*>(parent());
    ProfileManager* pm = island->m_pm;
    const auto& profiles = pm->getProfiles();
    for (int i = 0; i < profiles.size(); ++i) {
        QString name = profiles[i].name;
        if (!profiles[i].lastError.isEmpty()) name += " ⚠";
        QAction* action = m_profileMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(i == pm->getActiveIndex());
        connect(action, &QAction::triggered, [island, i]() { island->switchProfile(i); });
    }
    if (profiles.isEmpty()) {
        QAction* empty = m_profileMenu->addAction("(无配置)");
        empty->setEnabled(false);
    }
}

void ContextMenu::setPinned(bool on) { m_pinAction->setText(on ? "取消置顶" : "置顶窗口"); }

DynamicIsland::DynamicIsland(QWidget* parent)
    : QMainWindow(parent)
    , m_pm(new ProfileManager(this))
    , m_fetchWorker(new FetchWorker(m_pm, this))
    , m_refreshTimer(new QTimer(this))
    , m_frameTimer(new QTimer(this))
    , m_reDockTimer(new QTimer(this))
    , m_glowAnim(new QPropertyAnimation(this, "glowOffset"))
    , m_toggleGroup(nullptr)
    , m_fadeAnim(nullptr)
    , m_menu(new ContextMenu(this))
{
    setWindowIcon(QIcon(":/icon.ico"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    m_reDockTimer->setSingleShot(true);
    connect(m_reDockTimer, &QTimer::timeout, this, &DynamicIsland::reDockCheck);

    loadState();

    connect(m_fetchWorker, &FetchWorker::dataFetched, this, &DynamicIsland::onData);
    m_refreshTimer->setInterval(300000);
    connect(m_refreshTimer, &QTimer::timeout, this, &DynamicIsland::fetch);
    m_refreshTimer->start();
    fetch();

    m_frameTimer->setInterval(16);
    connect(m_frameTimer, &QTimer::timeout, this, &DynamicIsland::onTick);
    m_frameTimer->start();

    m_glowAnim->setDuration(2800);
    m_glowAnim->setStartValue(0.0);
    m_glowAnim->setEndValue(1.0);
    m_glowAnim->setLoopCount(-1);
    m_glowAnim->setEasingCurve(QEasingCurve::Linear);
    m_glowAnim->start();
}

void DynamicIsland::setGlowOffset(double v) { m_glowOffset = v; update(); }
void DynamicIsland::setItemOpacity(double v) { m_itemOpacity = v; update(); }
void DynamicIsland::setDockOpacity(double v) { m_dockOpacity = v; update(); }

QFont DynamicIsland::fontUi(int size, QFont::Weight weight) {
    QFont f;
    f.setFamilies({"Segoe UI Variable Display", "Microsoft YaHei UI", "PingFang SC", "SF Pro Display"});
    f.setPixelSize(size); f.setWeight(weight); f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont DynamicIsland::fontMono(int size, QFont::Weight weight) {
    QFont f;
    f.setFamilies({"JetBrains Mono", "Cascadia Code", "Consolas", "SF Mono"});
    f.setPixelSize(size); f.setWeight(weight); f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

void DynamicIsland::fetch() { m_fetchWorker->fetch(); }

void DynamicIsland::onData(const FetchResult& r) {
    if (r.ok) {
        m_data.planUsed = r.planUsed; m_data.planTotal = r.planTotal; m_data.planPercent = r.planPercent;
        m_data.compUsed = r.compUsed; m_data.compTotal = r.compTotal; m_data.compPercent = r.compPercent;
        m_data.monthUsed = r.monthUsed; m_data.monthTotal = r.monthTotal; m_data.monthPercent = r.monthPercent;
        if (r.compUsed > 0) { m_pct.target(r.compPercent); m_used.target(r.compUsed); }
        else { m_pct.target(r.planPercent); m_used.target(r.planUsed); }
        m_errorMsg.clear(); m_errorCode = 0;
    } else {
        m_errorMsg = r.msg; m_errorCode = r.code;
    }
}

void DynamicIsland::onTick() {
    m_phase += 0.04;
    bool pctC = m_pct.tick(); bool usedC = m_used.tick();
    m_glowSpeed += (m_glowTargetSpeed - m_glowSpeed) * 0.1;
    if (pctC || usedC || m_hovered || m_errorCode == 401 || m_docked) update();
}

void DynamicIsland::onFadeIn() {
    m_fadeAnim = new QPropertyAnimation(this, "itemOpacity");
    m_fadeAnim->setDuration(280); m_fadeAnim->setStartValue(0.0); m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->setEasingCurve(QEasingCurve::OutCubic); m_fadeAnim->start();
}

void DynamicIsland::toggleTop() {
    m_pinned = !m_pinned;
    Qt::WindowFlags f = windowFlags();
    m_pinned ? (f |= Qt::WindowStaysOnTopHint) : (f &= ~Qt::WindowStaysOnTopHint);
    setWindowFlags(f); show(); m_menu->setPinned(m_pinned);
}

void DynamicIsland::addCookie() {
    bool ok;
    QString text = QInputDialog::getMultiLineText(this, "添加Cookie",
        "粘贴 Cookie（支持格式）：\n• fetch 代码\n• curl 命令\n• 纯字符串\n\n获取：F12 → Network → 右键请求 → Copy as fetch", "", &ok);
    if (ok && !text.trimmed().isEmpty()) {
        QString cookieStr = text.trimmed();
        QRegularExpression re(R"(["']cookie["']\s*:\s*["'](.+?)["'])");
        QRegularExpressionMatch m = re.match(text);
        if (m.hasMatch()) cookieStr = m.captured(1);
        bool nameOk;
        QString name = QInputDialog::getText(this, "命名配置", "配置名称：", QLineEdit::Normal,
            QString("账号%1").arg(m_pm->getProfiles().size() + 1), &nameOk);
        if (nameOk && !name.trimmed().isEmpty()) { m_pm->add(name.trimmed(), cookieStr); m_menu->updateProfiles(); fetch(); }
    }
}

void DynamicIsland::deleteCurrentCookie() {
    CookieProfile* p = m_pm->getActive();
    if (p && QMessageBox::question(this, "确认删除", QString("删除配置 \"%1\" ？").arg(p->name)) == QMessageBox::Yes) {
        m_pm->remove(m_pm->getActiveIndex()); m_menu->updateProfiles(); fetch();
    }
}

void DynamicIsland::renameCurrentCookie() {
    CookieProfile* p = m_pm->getActive();
    if (p) {
        bool ok;
        QString name = QInputDialog::getText(this, "重命名", "新名称：", QLineEdit::Normal, p->name, &ok);
        if (ok && !name.trimmed().isEmpty()) { m_pm->rename(m_pm->getActiveIndex(), name.trimmed()); m_menu->updateProfiles(); }
    }
}

void DynamicIsland::switchProfile(int index) {
    m_pm->setActive(index); m_errorMsg.clear(); m_errorCode = 0; m_menu->updateProfiles(); fetch();
}

void DynamicIsland::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int w = width(), h = height();
    if (m_docked) { drawDocked(p, w, h); }
    else {
        QPainterPath path;
        double r = m_expanded ? 12 : qMin(h / 2.0, 22.0);
        path.addRoundedRect(QRectF(0, 0, w, h), r, r);
        p.setClipPath(path, Qt::IntersectClip);
        drawBg(p, w, h); drawGlow(p, w, h); drawBorder(p, w, h);
        m_expanded ? drawExpanded(p, w, h) : drawCompact(p, w, h);
    }
}

void DynamicIsland::drawDocked(QPainter& p, int w, int h) {
    double pct = m_pct.val();
    double a = m_dockOpacity;
    if (a < 0.01) return;
    bool isHoriz = m_dockMode == DOCK_TOP || m_dockMode == DOCK_BOTTOM;
    double r = isHoriz ? qMin(h / 2.0, 3.0) : qMin(w / 2.0, 3.0);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, w, h), r, r);
    p.setClipPath(path, Qt::IntersectClip);
    QLinearGradient bg(0, 0, w, h);
    bg.setColorAt(0, QColor(14, 14, 20, int(200 * a)));
    bg.setColorAt(1, QColor(18, 18, 26, int(200 * a)));
    p.setPen(Qt::NoPen); p.setBrush(bg);
    p.drawRoundedRect(QRectF(0, 0, w, h), r, r);
    double glowA = 0.15 + 0.05 * std::sin(m_phase);
    p.setBrush(QColor(70, 140, 255, int(255 * glowA * a)));
    p.drawRoundedRect(QRectF(-1, -1, w + 2, h + 2), r + 1, r + 1);
    if (isHoriz) {
        double fw = (w - 4) * qMin(pct / 100, 1.0);
        if (fw > 1) {
            QLinearGradient fg(2, 0, 2 + fw, 0);
            fg.setColorAt(0, QColor(59, 130, 246, int(255 * a)));
            fg.setColorAt(1, QColor(34, 211, 238, int(255 * a)));
            p.setBrush(fg); p.drawRoundedRect(QRectF(2, 2, fw, h - 4), r, r);
        }
    } else {
        double fh = (h - 4) * qMin(pct / 100, 1.0);
        if (fh > 1) {
            QLinearGradient fg(0, h - 2 - fh, 0, h - 2);
            fg.setColorAt(0, QColor(59, 130, 246, int(255 * a)));
            fg.setColorAt(1, QColor(34, 211, 238, int(255 * a)));
            p.setBrush(fg); p.drawRoundedRect(QRectF(2, h - 2 - fh, w - 4, fh), r, r);
        }
    }
}

void DynamicIsland::drawBg(QPainter& p, int w, int h) {
    QLinearGradient grad(0, 0, w, h);
    grad.setColorAt(0, QColor(14, 14, 20, 220));
    grad.setColorAt(0.5, QColor(18, 18, 26, 217));
    grad.setColorAt(1, QColor(14, 14, 20, 220));
    p.setPen(Qt::NoPen); p.setBrush(grad);
    double r = m_expanded ? 12 : qMin(h / 2.0, 22.0);
    p.drawRoundedRect(QRectF(0, 0, w, h), r, r);
}

void DynamicIsland::drawGlow(QPainter& p, int w, int h) {
    QColor g;
    if (m_errorCode == 401) {
        double a = 0.15 + 0.1 * std::sin(m_phase * 2);
        g = QColor(255, 80, 80, int(255 * a));
    } else {
        double a = 0.12 + 0.05 * std::sin(m_phase);
        if (m_hovered) a += 0.08;
        g = QColor(70, 140, 255, int(255 * a));
    }
    p.setPen(Qt::NoPen); p.setBrush(g);
    double r = m_expanded ? 14 : qMin(h / 2.0, 22.0) + 2;
    p.drawRoundedRect(QRectF(-1.5, -1.5, w + 3, h + 3), r, r);
}

void DynamicIsland::drawBorder(QPainter& p, int w, int h) {
    QPainterPath path;
    double r = m_expanded ? 12 : qMin(h / 2.0, 22.0);
    path.addRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), r, r);
    QLinearGradient bg(0, 0, w, 0);
    int ba;
    if (m_errorCode == 401) {
        ba = 40 + int(20 * std::sin(m_phase * 2));
        bg.setColorAt(0, QColor(255, 80, 80, ba));
        bg.setColorAt(0.5, QColor(255, 120, 80, ba));
        bg.setColorAt(1, QColor(255, 80, 80, ba));
    } else {
        ba = m_hovered ? 45 : 22;
        bg.setColorAt(0, QColor(70, 140, 255, ba));
        bg.setColorAt(0.33, QColor(100, 200, 255, ba));
        bg.setColorAt(0.66, QColor(140, 100, 255, ba));
        bg.setColorAt(1, QColor(70, 140, 255, ba));
    }
    p.setPen(QPen(QBrush(bg), 1.2)); p.setBrush(Qt::NoBrush); p.drawPath(path);
}

void DynamicIsland::drawFlowLight(QPainter& p, int w, int h) {
    QPainterPath path;
    double r = m_expanded ? 12 : qMin(h / 2.0, 22.0);
    path.addRoundedRect(QRectF(0, 0, w, h), r, r);
    double cx = w * m_glowOffset, bw = w * 0.4;
    QLinearGradient g(cx - bw, 0, cx + bw, 0);
    if (m_errorCode == 401) {
        g.setColorAt(0.0, QColor(255, 255, 255, 0));
        g.setColorAt(0.5, QColor(255, 100, 100, 15));
        g.setColorAt(1.0, QColor(255, 255, 255, 0));
    } else {
        g.setColorAt(0.0, QColor(255, 255, 255, 0));
        g.setColorAt(0.3, QColor(100, 200, 255, 8));
        g.setColorAt(0.5, QColor(100, 200, 255, 18));
        g.setColorAt(0.7, QColor(100, 200, 255, 8));
        g.setColorAt(1.0, QColor(255, 255, 255, 0));
    }
    p.setPen(Qt::NoPen); p.setBrush(g); p.drawPath(path);
}

void DynamicIsland::drawCompact(QPainter& p, int w, int h) {
    drawFlowLight(p, w, h);
    double pct = m_pct.val(), cy = h / 2.0;
    int ix = 30, isz = 8;
    p.setPen(Qt::NoPen);
    QLinearGradient ig(ix - 5, cy - 5, ix + 5, cy + 5);
    if (m_errorCode == 401) { ig.setColorAt(0, QColor(255, 100, 100)); ig.setColorAt(1, QColor(255, 60, 60)); }
    else { ig.setColorAt(0, QColor(100, 200, 255)); ig.setColorAt(1, Colors::accent()); }
    p.setBrush(ig); p.drawEllipse(ix - isz / 2, int(cy - isz / 2), isz, isz);
    if (!m_errorMsg.isEmpty()) {
        p.setPen(m_errorCode == 401 ? Colors::error() : QColor(255, 200, 100));
        p.setFont(fontUi(9)); p.drawText(QRectF(46, 0, 100, h), Qt::AlignVCenter | Qt::AlignLeft, m_errorMsg);
    } else {
        p.setPen(Colors::text()); p.setFont(fontMono(15, QFont::DemiBold));
        p.drawText(QRectF(46, 0, 55, h), Qt::AlignVCenter | Qt::AlignLeft, QString::number(pct, 'f', 1));
        p.setPen(Colors::dim()); p.setFont(fontUi(10));
        p.drawText(QRectF(108, 0, 30, h), Qt::AlignVCenter | Qt::AlignLeft, "%");
    }
}

void DynamicIsland::drawExpanded(QPainter& p, int w, int h) {
    drawFlowLight(p, w, h);
    int pad = 28; double a = m_itemOpacity;
    if (a < 0.01) return;
    int alpha = int(255 * a);
    QColor titleC(235, 237, 245, alpha), valC(240, 242, 250, alpha), dimC(80, 85, 100, alpha), errC(255, 100, 100, alpha);
    p.setPen(titleC); p.setFont(fontUi(13, QFont::DemiBold));
    p.drawText(QRectF(pad, 22, 200, 24), Qt::AlignVCenter | Qt::AlignLeft, "MiMo Token Monitor");
    CookieProfile* profile = m_pm->getActive();
    if (profile) {
        QString pt = profile->name;
        if (!profile->lastError.isEmpty()) pt += " ⚠";
        p.setPen(QColor(100, 200, 255, int(alpha * 0.7))); p.setFont(fontUi(9));
        p.drawText(QRectF(pad, 42, 200, 16), Qt::AlignVCenter | Qt::AlignLeft, pt);
    }
    int y0 = 66, rh = 24;
    if (!m_errorMsg.isEmpty()) {
        if (m_errorCode == 401) {
            p.setPen(errC); p.setFont(fontUi(11));
            p.drawText(QRectF(pad, y0, w - pad * 2, rh), Qt::AlignCenter, "Cookie 已过期");
            p.setPen(dimC); p.setFont(fontUi(9));
            p.drawText(QRectF(pad, y0 + rh + 4, w - pad * 2, rh), Qt::AlignCenter, "右键 → 添加新Cookie");
        } else {
            p.setPen(QColor(255, 200, 100, alpha)); p.setFont(fontUi(11));
            p.drawText(QRectF(pad, y0, w - pad * 2, rh * 2), Qt::AlignCenter, m_errorMsg);
        }
    } else {
        if (m_data.compTotal > 0) {
            p.setPen(QColor(100, 255, 180, alpha)); p.setFont(fontUi(9, QFont::Light));
            p.drawText(QRectF(pad, y0, 60, rh), Qt::AlignVCenter | Qt::AlignLeft, "补偿");
            p.setPen(valC); p.setFont(fontMono(10, QFont::Medium));
            p.drawText(QRectF(pad + 60, y0, 180, rh), Qt::AlignVCenter | Qt::AlignLeft,
                QString("%1 / %2").arg(qlonglong(m_data.compUsed)).arg(qlonglong(m_data.compTotal)));
            y0 += rh;
        }
        if (m_data.planTotal > 0) {
            p.setPen(QColor(100, 200, 255, alpha)); p.setFont(fontUi(9, QFont::Light));
            p.drawText(QRectF(pad, y0, 60, rh), Qt::AlignVCenter | Qt::AlignLeft, "套餐");
            p.setPen(valC); p.setFont(fontMono(10, QFont::Medium));
            p.drawText(QRectF(pad + 60, y0, 180, rh), Qt::AlignVCenter | Qt::AlignLeft,
                QString("%1 / %2").arg(qlonglong(m_data.planUsed)).arg(qlonglong(m_data.planTotal)));
            y0 += rh;
        }
        int by = y0 + 8; double bx = pad, bw = w - pad * 2, bh = 4, br = 2;
        p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 12));
        p.drawRoundedRect(QRectF(bx, by, bw, bh), br, br);
        double pct = m_pct.val(), fw = bw * qMin(pct / 100, 1.0);
        if (fw > 1) {
            QLinearGradient fg(bx, 0, bx + fw, 0);
            fg.setColorAt(0, QColor(59, 130, 246, alpha));
            fg.setColorAt(1, QColor(34, 211, 238, alpha));
            p.setBrush(fg); p.drawRoundedRect(QRectF(bx, by, fw, bh), br, br);
        }
        p.setPen(QColor(200, 205, 220, alpha)); p.setFont(fontMono(10, QFont::Medium));
        p.drawText(QRectF(pad, by + bh + 8, 80, 18), Qt::AlignVCenter | Qt::AlignLeft, QString::number(pct, 'f', 2) + "%");
    }
    p.setPen(dimC); p.setFont(fontUi(9, QFont::Light));
    p.drawText(QRectF(pad, h - 24, w - pad * 2, 14), Qt::AlignCenter, "右键菜单 · 左键收起");
}

void DynamicIsland::enterEvent(QEnterEvent*) {
    m_hovered = true; m_glowTargetSpeed = 0.002; setCursor(Qt::PointingHandCursor);
    if (m_docked && !m_dockHovered && !m_animating) {
        m_dockHovered = true; m_undockedFromDock = true; m_animating = true; undock();
    }
}

void DynamicIsland::leaveEvent(QEvent*) {
    m_hovered = false; m_glowTargetSpeed = 0.0008; setCursor(Qt::ArrowCursor);
    if (m_undockedFromDock && !m_mousePressed && !m_menuOpen) {
        m_animating = true; m_reDockTimer->start(400);
    }
    m_dockHovered = false;
}

void DynamicIsland::reDockCheck() {
    if (m_undockedFromDock && !m_mousePressed && !m_hovered) dock(m_lastDockMode);
    m_animating = false;
}

void DynamicIsland::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_reDockTimer->stop(); m_animating = false;
        if (m_docked) return;
        m_mousePressed = true; m_undockedFromDock = false;
        m_dragPos = e->globalPosition().toPoint() - pos(); m_dragMoved = false;
    } else if (e->button() == Qt::RightButton) {
        if (m_docked) return;
        m_menuOpen = true;
        m_menu->updateProfiles(); m_menu->setPinned(m_pinned);
        m_menu->exec(e->globalPosition().toPoint());
        m_menuOpen = false;
    }
}

void DynamicIsland::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton && !m_docked) {
        QPoint np = e->globalPosition().toPoint() - m_dragPos;
        if ((np - pos()).manhattanLength() > 3) { m_dragMoved = true; move(np); }
    }
}

void DynamicIsland::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_mousePressed = false;
        if (m_docked) return;
        if (!m_dragMoved) toggle();
        else { checkDockDuringDrag(); m_undockedFromDock = false; }
    }
}

void DynamicIsland::checkDockDuringDrag() {
    if (m_expanded || m_docked) return;
    QRect screen = QApplication::primaryScreen()->availableGeometry();
    QPoint p = pos(); int w = width(), h = height();
    DockMode mode = DOCK_NONE;
    if (p.y() <= screen.top() + DOCK_THRESHOLD) mode = DOCK_TOP;
    else if (p.y() + h >= screen.bottom() - DOCK_THRESHOLD) mode = DOCK_BOTTOM;
    else if (p.x() <= screen.left() + DOCK_THRESHOLD) mode = DOCK_LEFT;
    else if (p.x() + w >= screen.right() - DOCK_THRESHOLD) mode = DOCK_RIGHT;
    if (mode != DOCK_NONE) dock(mode);
}

void DynamicIsland::dock(DockMode mode) {
    m_docked = true; m_dockMode = mode; m_lastDockMode = mode; m_expanded = false;
    QRect screen = QApplication::primaryScreen()->availableGeometry();
    QRect cur = geometry(); int cx = cur.center().x(), cy = cur.center().y();
    QRect target;
    switch (mode) {
        case DOCK_TOP: target = QRect(cx - DOCKED_LEN / 2, screen.top(), DOCKED_LEN, DOCKED_H); break;
        case DOCK_BOTTOM: target = QRect(cx - DOCKED_LEN / 2, screen.bottom() - DOCKED_H, DOCKED_LEN, DOCKED_H); break;
        case DOCK_LEFT: target = QRect(screen.left(), cy - DOCKED_LEN / 2, DOCKED_W, DOCKED_LEN); break;
        case DOCK_RIGHT: target = QRect(screen.right() - DOCKED_W, cy - DOCKED_LEN / 2, DOCKED_W, DOCKED_LEN); break;
        default: break;
    }
    hide(); setGeometry(target); m_dockOpacity = 1.0; show(); saveState();
}

void DynamicIsland::undock() {
    m_docked = false; m_dockHovered = false; m_undockedFromDock = true;
    QRect cur = geometry();
    QRect screen = QApplication::primaryScreen()->availableGeometry();
    static constexpr int VISIBLE = 146;
    int nx = cur.center().x() - COMPACT_W / 2, ny = cur.center().y() - COMPACT_H / 2;
    switch (m_lastDockMode) {
        case DOCK_TOP: ny = screen.top(); break;
        case DOCK_BOTTOM: ny = screen.bottom() - COMPACT_H; break;
        case DOCK_LEFT: nx = screen.left() - (COMPACT_W - VISIBLE); break;
        case DOCK_RIGHT: nx = screen.right() - VISIBLE; break;
        default: break;
    }
    nx = qBound(screen.left() - (COMPACT_W - VISIBLE), nx, screen.right() - VISIBLE);
    ny = qBound(screen.top(), ny, screen.bottom() - COMPACT_H);
    QRect target(nx, ny, COMPACT_W, COMPACT_H);
    QParallelAnimationGroup* grp = new QParallelAnimationGroup(this);
    QPropertyAnimation* geo = new QPropertyAnimation(this, "geometry");
    geo->setDuration(350); geo->setEasingCurve(QEasingCurve::OutExpo);
    geo->setStartValue(cur); geo->setEndValue(target); grp->addAnimation(geo);
    QPropertyAnimation* opac = new QPropertyAnimation(this, "dockOpacity");
    opac->setDuration(250); opac->setStartValue(1.0); opac->setEndValue(0.0);
    opac->setEasingCurve(QEasingCurve::OutCubic); grp->addAnimation(opac);
    grp->start(); saveState();
}

void DynamicIsland::toggle() {
    int tw = m_expanded ? COMPACT_W : EXPANDED_W, th = m_expanded ? COMPACT_H : EXPANDED_H;
    m_expanded = !m_expanded;
    m_toggleGroup = new QParallelAnimationGroup(this);
    QPropertyAnimation* geo = new QPropertyAnimation(this, "geometry");
    geo->setDuration(420); geo->setEasingCurve(QEasingCurve::OutExpo);
    QRect cur = geometry();
    geo->setStartValue(cur); geo->setEndValue(QRect(cur.center().x() - tw / 2, cur.top(), tw, th));
    m_toggleGroup->addAnimation(geo);
    if (m_expanded) { m_itemOpacity = 0.0; QTimer::singleShot(100, this, &DynamicIsland::onFadeIn); }
    else {
        m_itemOpacity = 1.0;
        QPropertyAnimation* opac = new QPropertyAnimation(this, "itemOpacity");
        opac->setDuration(100); opac->setStartValue(1.0); opac->setEndValue(0.0);
        opac->setEasingCurve(QEasingCurve::InCubic); m_toggleGroup->addAnimation(opac);
    }
    m_toggleGroup->start();
}

void DynamicIsland::loadState() {
    QString statePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.mimo_monitor/state.json";
    QFile f(statePath);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        int dm = obj["dock_mode"].toInt(0);
        int x = obj["x"].toInt(-1), y = obj["y"].toInt(-1);
        if (dm > 0 && x >= 0 && y >= 0) {
            m_dockMode = static_cast<DockMode>(dm);
            m_lastDockMode = m_dockMode;
            m_docked = true; m_dockOpacity = 1.0;
            (dm == DOCK_TOP || dm == DOCK_BOTTOM) ? resize(DOCKED_LEN, DOCKED_H) : resize(DOCKED_W, DOCKED_LEN);
            move(x, y); return;
        }
    }
    QRect s = QApplication::primaryScreen()->availableGeometry();
    move((s.width() - COMPACT_W) / 2, s.top() + 12); resize(COMPACT_W, COMPACT_H);
}

void DynamicIsland::saveState() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.mimo_monitor";
    QDir().mkpath(dir);
    QJsonObject obj;
    obj["dock_mode"] = m_docked ? static_cast<int>(m_dockMode) : 0;
    obj["x"] = pos().x(); obj["y"] = pos().y();
    QFile f(dir + "/state.json");
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(obj).toJson()); f.close(); }
}

void DynamicIsland::closeEvent(QCloseEvent* e) { saveState(); QMainWindow::closeEvent(e); }
