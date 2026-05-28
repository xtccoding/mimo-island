#include "DynamicIsland.h"
#include <QRegularExpression>

// SpringValue 实现
SpringValue::SpringValue(double v)
    : m_cur(v), m_tgt(v), m_vel(0)
{
}

void SpringValue::target(double t)
{
    m_tgt = t;
}

bool SpringValue::tick(double dt, double spring, double damp)
{
    double d = m_tgt - m_cur;
    m_vel += d * spring * dt;
    m_vel *= qMax(0.0, 1 - damp * dt);
    m_cur += m_vel * dt;
    return std::abs(d) > 0.0005 || std::abs(m_vel) > 0.005;
}

// ContextMenu 实现
ContextMenu::ContextMenu(QWidget* parent)
    : QMenu(parent)
{
    setStyleSheet(R"(
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
    )");
    
    buildMenu();
}

void ContextMenu::buildMenu()
{
    clear();
    
    m_profileMenu = addMenu("切换账号");
    m_profileMenu->setStyleSheet(styleSheet());
    
    QAction* addAct = QMenu::addAction("添加Cookie");
    connect(addAct, &QAction::triggered, qobject_cast<DynamicIsland*>(parent()), &DynamicIsland::addCookie);
    
    QAction* delAct = QMenu::addAction("删除当前");
    connect(delAct, &QAction::triggered, qobject_cast<DynamicIsland*>(parent()), &DynamicIsland::deleteCurrentCookie);
    
    addSeparator();
    
    QAction* refreshAct = QMenu::addAction("刷新数据");
    connect(refreshAct, &QAction::triggered, qobject_cast<DynamicIsland*>(parent()), &DynamicIsland::fetch);
    
    addSeparator();
    
    m_pinAction = QMenu::addAction("置顶窗口");
    connect(m_pinAction, &QAction::triggered, qobject_cast<DynamicIsland*>(parent()), &DynamicIsland::toggleTop);
    
    addSeparator();
    
    QAction* quitAct = QMenu::addAction("退出程序");
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);
}

void ContextMenu::updateProfiles()
{
    m_profileMenu->clear();
    
    DynamicIsland* island = qobject_cast<DynamicIsland*>(parent());
    if (!island) return;
    
    ProfileManager* pm = island->m_pm;
    const auto& profiles = pm->getProfiles();
    
    for (int i = 0; i < profiles.size(); ++i) {
        QString name = profiles[i].name;
        if (!profiles[i].lastError.isEmpty()) {
            name += " ⚠";
        }
        
        QAction* action = m_profileMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(i == pm->getActiveIndex());
        connect(action, &QAction::triggered, [this, i]() {
            qobject_cast<DynamicIsland*>(parent())->switchProfile(i);
        });
    }
    
    if (profiles.isEmpty()) {
        QAction* empty = m_profileMenu->addAction("(无配置)");
        empty->setEnabled(false);
    }
}

void ContextMenu::setPinned(bool on)
{
    m_pinAction->setText(on ? "取消置顶" : "置顶窗口");
}

// DynamicIsland 实现
DynamicIsland::DynamicIsland(QWidget* parent)
    : QMainWindow(parent)
    , m_pm(new ProfileManager(this))
    , m_fetchWorker(new FetchWorker(m_pm, this))
    , m_refreshTimer(new QTimer(this))
    , m_frameTimer(new QTimer(this))
    , m_glowAnim(new QPropertyAnimation(this, "glowOffset"))
    , m_toggleGroup(nullptr)
    , m_fadeAnim(nullptr)
    , m_menu(new ContextMenu(this))
{
    setWindowIcon(QIcon(":/icon.ico"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    QScreen* screen = QApplication::primaryScreen();
    QRect geo = screen->geometry();
    move((geo.width() - COMPACT_W) / 2, 12);
    resize(COMPACT_W, COMPACT_H);

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

void DynamicIsland::setGlowOffset(double v)
{
    m_glowOffset = v;
    update();
}

void DynamicIsland::setItemOpacity(double v)
{
    m_itemOpacity = v;
    update();
}

QFont DynamicIsland::fontUi(int size, QFont::Weight weight)
{
    QFont f;
    f.setFamilies({"Segoe UI Variable Display", "Microsoft YaHei UI", "PingFang SC", "SF Pro Display"});
    f.setPixelSize(size);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont DynamicIsland::fontMono(int size, QFont::Weight weight)
{
    QFont f;
    f.setFamilies({"JetBrains Mono", "Cascadia Code", "Consolas", "SF Mono"});
    f.setPixelSize(size);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

void DynamicIsland::fetch()
{
    m_fetchWorker->fetch();
}

void DynamicIsland::onData(const FetchResult& result)
{
    if (result.ok) {
        m_data.planUsed = result.planUsed;
        m_data.planTotal = result.planTotal;
        m_data.planPercent = result.planPercent;
        m_data.compUsed = result.compUsed;
        m_data.compTotal = result.compTotal;
        m_data.compPercent = result.compPercent;
        m_data.monthUsed = result.monthUsed;
        m_data.monthTotal = result.monthTotal;
        m_data.monthPercent = result.monthPercent;
        
        if (result.compUsed > 0) {
            m_pct.target(result.compPercent);
            m_used.target(result.compUsed);
        } else {
            m_pct.target(result.planPercent);
            m_used.target(result.planUsed);
        }
        
        m_errorMsg.clear();
        m_errorCode = 0;
    } else {
        m_errorMsg = result.msg;
        m_errorCode = result.code;
    }
}

void DynamicIsland::onTick()
{
    m_phase += 0.04;
    bool pctChanged = m_pct.tick();
    bool usedChanged = m_used.tick();
    m_glowSpeed += (m_glowTargetSpeed - m_glowSpeed) * 0.1;
    
    if (pctChanged || usedChanged || m_hovered || m_errorCode == 401) {
        update();
    }
}

void DynamicIsland::onFadeIn()
{
    m_fadeAnim = new QPropertyAnimation(this, "itemOpacity");
    m_fadeAnim->setDuration(280);
    m_fadeAnim->setStartValue(0.0);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_fadeAnim->start();
}

void DynamicIsland::addCookie()
{
    bool ok;
    QString text = QInputDialog::getMultiLineText(
        this, "添加Cookie",
        "粘贴 Cookie（支持格式）：\n"
        "• fetch 代码\n"
        "• curl 命令\n"
        "• 纯字符串\n\n"
        "获取：F12 → Network → 右键请求 → Copy as fetch",
        "", &ok
    );
    
    if (ok && !text.trimmed().isEmpty()) {
        // 简单的 Cookie 提取
        QString cookieStr = text.trimmed();
        
        QRegularExpression fetchRe(R"(["']cookie["']\s*:\s*["'](.+?)["'])");
        QRegularExpressionMatch match = fetchRe.match(text);
        if (match.hasMatch()) {
            cookieStr = match.captured(1);
        }
        
        bool nameOk;
        QString name = QInputDialog::getText(
            this, "命名配置", "配置名称：",
            QLineEdit::Normal,
            QString("账号%1").arg(m_pm->getProfiles().size() + 1),
            &nameOk
        );
        
        if (nameOk && !name.trimmed().isEmpty()) {
            m_pm->add(name.trimmed(), cookieStr);
            m_menu->updateProfiles();
            fetch();
        }
    }
}

void DynamicIsland::deleteCurrentCookie()
{
    CookieProfile* profile = m_pm->getActive();
    if (profile) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "确认删除",
            QString("删除配置 \"%1\" ？").arg(profile->name),
            QMessageBox::Yes | QMessageBox::No
        );
        
        if (reply == QMessageBox::Yes) {
            m_pm->remove(m_pm->getActiveIndex());
            m_menu->updateProfiles();
            fetch();
        }
    }
}

void DynamicIsland::switchProfile(int index)
{
    m_pm->setActive(index);
    m_errorMsg.clear();
    m_errorCode = 0;
    m_menu->updateProfiles();
    fetch();
}

void DynamicIsland::toggleTop()
{
    m_pinned = !m_pinned;
    Qt::WindowFlags f = windowFlags();
    if (m_pinned) {
        f |= Qt::WindowStaysOnTopHint;
    } else {
        f &= ~Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(f);
    show();
    m_menu->setPinned(m_pinned);
}

void DynamicIsland::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int w = width();
    int h = height();

    QPainterPath path;
    double r = m_expanded ? 12 : qMin(h / 2.0, 22.0);
    path.addRoundedRect(QRectF(0, 0, w, h), r, r);
    p.setClipPath(path, Qt::IntersectClip);

    drawBg(p, w, h);
    drawGlow(p, w, h);
    drawBorder(p, w, h);

    if (!m_expanded) {
        drawCompact(p, w, h);
    } else {
        drawExpanded(p, w, h);
    }
}

void DynamicIsland::drawBg(QPainter& p, int w, int h)
{
    QLinearGradient grad(0, 0, w, h);
    grad.setColorAt(0, QColor(14, 14, 20, 220));
    grad.setColorAt(0.5, QColor(18, 18, 26, 217));
    grad.setColorAt(1, QColor(14, 14, 20, 220));
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    double r = m_expanded ? 12 : qMin(h / 2.0, 22.0);
    p.drawRoundedRect(QRectF(0, 0, w, h), r, r);
}

void DynamicIsland::drawGlow(QPainter& p, int w, int h)
{
    QColor g;
    if (m_errorCode == 401) {
        double glowA = 0.15 + 0.1 * std::sin(m_phase * 2);
        g = QColor(255, 80, 80, static_cast<int>(255 * glowA));
    } else {
        double glowA = 0.12 + 0.05 * std::sin(m_phase);
        if (m_hovered) glowA += 0.08;
        g = QColor(70, 140, 255, static_cast<int>(255 * glowA));
    }
    
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    double r = m_expanded ? 14 : qMin(h / 2.0, 22.0) + 2;
    p.drawRoundedRect(QRectF(-1.5, -1.5, w + 3, h + 3), r, r);
}

void DynamicIsland::drawBorder(QPainter& p, int w, int h)
{
    QPainterPath path;
    double r = m_expanded ? 12 : qMin(h / 2.0, 22.0);
    path.addRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), r, r);

    QLinearGradient bg(0, 0, w, 0);
    int ba;
    
    if (m_errorCode == 401) {
        ba = 40 + static_cast<int>(20 * std::sin(m_phase * 2));
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
    
    p.setPen(QPen(QBrush(bg), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void DynamicIsland::drawFlowLight(QPainter& p, int w, int h)
{
    QPainterPath path;
    double r = m_expanded ? 12 : qMin(h / 2.0, 22.0);
    path.addRoundedRect(QRectF(0, 0, w, h), r, r);

    double cx = w * m_glowOffset;
    double bandW = w * 0.4;

    QLinearGradient g(cx - bandW, 0, cx + bandW, 0);
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

    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawPath(path);
}

void DynamicIsland::drawCompact(QPainter& p, int w, int h)
{
    drawFlowLight(p, w, h);
    double pct = m_pct.val();
    double cy = h / 2.0;

    int ix = 30;
    int isz = 8;
    p.setPen(Qt::NoPen);
    
    QLinearGradient ig(ix - 5, cy - 5, ix + 5, cy + 5);
    if (m_errorCode == 401) {
        ig.setColorAt(0, QColor(255, 100, 100));
        ig.setColorAt(1, QColor(255, 60, 60));
    } else {
        ig.setColorAt(0, QColor(100, 200, 255));
        ig.setColorAt(1, Colors::accent());
    }
    p.setBrush(ig);
    p.drawEllipse(ix - isz / 2, static_cast<int>(cy - isz / 2), isz, isz);

    if (!m_errorMsg.isEmpty()) {
        p.setPen(m_errorCode == 401 ? Colors::error() : QColor(255, 200, 100));
        p.setFont(fontUi(9));
        p.drawText(QRectF(46, 0, 100, h), Qt::AlignVCenter | Qt::AlignLeft, m_errorMsg);
    } else {
        p.setPen(Colors::text());
        p.setFont(fontMono(15, QFont::DemiBold));
        p.drawText(QRectF(46, 0, 55, h), Qt::AlignVCenter | Qt::AlignLeft, QString::number(pct, 'f', 1));

        p.setPen(Colors::dim());
        p.setFont(fontUi(10));
        p.drawText(QRectF(108, 0, 30, h), Qt::AlignVCenter | Qt::AlignLeft, "%");
    }
}

void DynamicIsland::drawExpanded(QPainter& p, int w, int h)
{
    drawFlowLight(p, w, h);
    int pad = 28;
    double a = m_itemOpacity;
    if (a < 0.01) return;

    int alpha = static_cast<int>(255 * a);

    QColor titleColor(235, 237, 245, alpha);
    QColor labelColor(120, 125, 145, alpha);
    QColor valColor(240, 242, 250, alpha);
    QColor dimColor(80, 85, 100, alpha);
    QColor errorColor(255, 100, 100, alpha);

    p.setPen(titleColor);
    p.setFont(fontUi(13, QFont::DemiBold));
    p.drawText(QRectF(pad, 22, 200, 24), Qt::AlignVCenter | Qt::AlignLeft, "MiMo Token Monitor");

    CookieProfile* profile = m_pm->getActive();
    if (profile) {
        QString profileText = profile->name;
        if (!profile->lastError.isEmpty()) {
            profileText += " ⚠";
        }
        p.setPen(QColor(100, 200, 255, static_cast<int>(alpha * 0.7)));
        p.setFont(fontUi(9));
        p.drawText(QRectF(pad, 42, 200, 16), Qt::AlignVCenter | Qt::AlignLeft, profileText);
    }

    int y0 = 66;
    int rh = 24;

    if (!m_errorMsg.isEmpty()) {
        if (m_errorCode == 401) {
            p.setPen(errorColor);
            p.setFont(fontUi(11));
            p.drawText(QRectF(pad, y0, w - pad * 2, rh), Qt::AlignCenter, "Cookie 已过期");
            p.setPen(dimColor);
            p.setFont(fontUi(9));
            p.drawText(QRectF(pad, y0 + rh + 4, w - pad * 2, rh), Qt::AlignCenter, "右键 → 添加新Cookie");
        } else {
            p.setPen(QColor(255, 200, 100, alpha));
            p.setFont(fontUi(11));
            p.drawText(QRectF(pad, y0, w - pad * 2, rh * 2), Qt::AlignCenter, m_errorMsg);
        }
    } else {
        if (m_data.compTotal > 0) {
            p.setPen(QColor(100, 255, 180, alpha));
            p.setFont(fontUi(9, QFont::Light));
            p.drawText(QRectF(pad, y0, 60, rh), Qt::AlignVCenter | Qt::AlignLeft, "补偿");
            p.setPen(valColor);
            p.setFont(fontMono(10, QFont::Medium));
            p.drawText(QRectF(pad + 60, y0, 180, rh), Qt::AlignVCenter | Qt::AlignLeft,
                QString("%1 / %2").arg(static_cast<qlonglong>(m_data.compUsed)).arg(static_cast<qlonglong>(m_data.compTotal)));
            y0 += rh;
        }
        
        if (m_data.planTotal > 0) {
            p.setPen(QColor(100, 200, 255, alpha));
            p.setFont(fontUi(9, QFont::Light));
            p.drawText(QRectF(pad, y0, 60, rh), Qt::AlignVCenter | Qt::AlignLeft, "套餐");
            p.setPen(valColor);
            p.setFont(fontMono(10, QFont::Medium));
            p.drawText(QRectF(pad + 60, y0, 180, rh), Qt::AlignVCenter | Qt::AlignLeft,
                QString("%1 / %2").arg(static_cast<qlonglong>(m_data.planUsed)).arg(static_cast<qlonglong>(m_data.planTotal)));
            y0 += rh;
        }
        
        int by = y0 + 8;
        double bx = pad;
        double bw = w - pad * 2;
        double bh = 4.0;
        double br = 2.0;

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 12));
        p.drawRoundedRect(QRectF(bx, by, bw, bh), br, br);

        double pct = m_pct.val();
        double fw = bw * qMin(pct / 100, 1.0);
        if (fw > 1) {
            QLinearGradient fg(bx, 0, bx + fw, 0);
            fg.setColorAt(0, QColor(59, 130, 246, alpha));
            fg.setColorAt(1, QColor(34, 211, 238, alpha));
            p.setBrush(fg);
            p.drawRoundedRect(QRectF(bx, by, fw, bh), br, br);
        }

        p.setPen(QColor(200, 205, 220, alpha));
        p.setFont(fontMono(10, QFont::Medium));
        p.drawText(QRectF(pad, by + bh + 8, 80, 18), Qt::AlignVCenter | Qt::AlignLeft,
            QString::number(pct, 'f', 2) + "%");
    }

    p.setPen(dimColor);
    p.setFont(fontUi(9, QFont::Light));
    p.drawText(QRectF(pad, h - 24, w - pad * 2, 14), Qt::AlignCenter, "右键菜单 · 左键收起");
}

void DynamicIsland::enterEvent(QEnterEvent*)
{
    m_hovered = true;
    m_glowTargetSpeed = 0.002;
    setCursor(Qt::PointingHandCursor);
}

void DynamicIsland::leaveEvent(QEvent*)
{
    m_hovered = false;
    m_glowTargetSpeed = 0.0008;
    setCursor(Qt::ArrowCursor);
}

void DynamicIsland::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragPos = e->globalPosition().toPoint() - pos();
        m_dragMoved = false;
    } else if (e->button() == Qt::RightButton) {
        m_menu->updateProfiles();
        m_menu->setPinned(m_pinned);
        m_menu->exec(e->globalPosition().toPoint());
    }
}

void DynamicIsland::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton) {
        QPoint np = e->globalPosition().toPoint() - m_dragPos;
        if ((np - pos()).manhattanLength() > 3) {
            m_dragMoved = true;
            move(np);
        }
    }
}

void DynamicIsland::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !m_dragMoved) {
        toggle();
    }
}

void DynamicIsland::toggle()
{
    int tw = m_expanded ? COMPACT_W : EXPANDED_W;
    int th = m_expanded ? COMPACT_H : EXPANDED_H;

    m_expanded = !m_expanded;

    m_toggleGroup = new QParallelAnimationGroup(this);

    QPropertyAnimation* geo = new QPropertyAnimation(this, "geometry");
    geo->setDuration(420);
    geo->setEasingCurve(QEasingCurve::OutExpo);
    QRect cur = geometry();
    int nx = cur.center().x() - tw / 2;
    geo->setStartValue(cur);
    geo->setEndValue(QRect(nx, cur.top(), tw, th));
    m_toggleGroup->addAnimation(geo);

    if (m_expanded) {
        m_itemOpacity = 0.0;
        QTimer::singleShot(100, this, &DynamicIsland::onFadeIn);
    } else {
        m_itemOpacity = 1.0;
        QPropertyAnimation* opac = new QPropertyAnimation(this, "itemOpacity");
        opac->setDuration(100);
        opac->setStartValue(1.0);
        opac->setEndValue(0.0);
        opac->setEasingCurve(QEasingCurve::InCubic);
        m_toggleGroup->addAnimation(opac);
    }

    m_toggleGroup->start();
}
