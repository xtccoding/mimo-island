#pragma once

#include <QMainWindow>
#include <QMenu>
#include <QAction>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QTimer>
#include <QPoint>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

#include "ProfileManager.h"
#include "FetchThread.h"

namespace Colors {
    inline QColor text() { return QColor(235, 237, 245); }
    inline QColor label() { return QColor(120, 125, 145); }
    inline QColor dim() { return QColor(80, 85, 100); }
    inline QColor accent() { return QColor(70, 140, 255); }
    inline QColor error() { return QColor(255, 100, 100); }
    inline QColor bg0() { return QColor(14, 14, 20, 220); }
    inline QColor bg1() { return QColor(18, 18, 26, 217); }
}

class SpringValue {
public:
    SpringValue(double v = 0);
    void target(double t);
    bool tick(double dt = 0.016, double spring = 100, double damp = 14);
    double val() const { return m_cur; }
private:
    double m_cur, m_tgt, m_vel;
};

enum DockMode { DOCK_NONE = 0, DOCK_TOP = 1, DOCK_BOTTOM = 2, DOCK_LEFT = 3, DOCK_RIGHT = 4 };

class ContextMenu : public QMenu {
    Q_OBJECT
public:
    explicit ContextMenu(QWidget* parent);
    void updateProfiles();
    void setPinned(bool on);
private:
    void buildMenu();
    QMenu* m_profileMenu;
    QAction* m_pinAction;
};

class DynamicIsland : public QMainWindow {
    Q_OBJECT
    Q_PROPERTY(double glowOffset READ glowOffset WRITE setGlowOffset)
    Q_PROPERTY(double itemOpacity READ itemOpacity WRITE setItemOpacity)
    Q_PROPERTY(double dockOpacity READ dockOpacity WRITE setDockOpacity)

    friend class ContextMenu;

public:
    explicit DynamicIsland(QWidget* parent = nullptr);

    double glowOffset() const { return m_glowOffset; }
    void setGlowOffset(double v);
    double itemOpacity() const { return m_itemOpacity; }
    void setItemOpacity(double v);
    double dockOpacity() const { return m_dockOpacity; }
    void setDockOpacity(double v);

public slots:
    void addCookie();
    void deleteCurrentCookie();
    void renameCurrentCookie();
    void switchProfile(int index);
    void toggleTop();
    void fetch();

protected:
    void paintEvent(QPaintEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onData(const FetchResult& result);
    void onTick();
    void onFadeIn();
    void reDockCheck();

private:
    void drawBg(QPainter& p, int w, int h);
    void drawGlow(QPainter& p, int w, int h);
    void drawBorder(QPainter& p, int w, int h);
    void drawFlowLight(QPainter& p, int w, int h);
    void drawCompact(QPainter& p, int w, int h);
    void drawExpanded(QPainter& p, int w, int h);
    void drawDocked(QPainter& p, int w, int h);
    void toggle();
    void checkDockDuringDrag();
    void dock(DockMode mode);
    void undock();
    void loadState();
    void saveState();

    QFont fontUi(int size = 10, QFont::Weight weight = QFont::Normal);
    QFont fontMono(int size = 11, QFont::Weight weight = QFont::Medium);

    static constexpr int COMPACT_W = 168;
    static constexpr int COMPACT_H = 44;
    static constexpr int EXPANDED_W = 320;
    static constexpr int EXPANDED_H = 195;
    static constexpr int DOCKED_H = 6;
    static constexpr int DOCKED_W = 6;
    static constexpr int DOCKED_LEN = 200;
    static constexpr int DOCK_THRESHOLD = 10;

    bool m_expanded = false;
    bool m_hovered = false;
    bool m_pinned = true;
    QPoint m_dragPos;
    bool m_dragMoved = false;
    double m_phase = 0.0;
    double m_itemOpacity = 1.0;
    QString m_errorMsg;
    int m_errorCode = 0;

    bool m_docked = false;
    bool m_dockHovered = false;
    bool m_undockedFromDock = false;
    bool m_mousePressed = false;
    bool m_menuOpen = false;
    bool m_animating = false;
    DockMode m_dockMode = DOCK_NONE;
    DockMode m_lastDockMode = DOCK_TOP;
    double m_dockOpacity = 0.0;

    struct TokenData {
        double planUsed = 0, planTotal = 0, planPercent = 0;
        double compUsed = 0, compTotal = 0, compPercent = 0;
        double monthUsed = 0, monthTotal = 0, monthPercent = 0;
    } m_data;

    SpringValue m_pct;
    SpringValue m_used;

    double m_glowOffset = 0.0;
    double m_glowSpeed = 0.0008;
    double m_glowTargetSpeed = 0.0008;

    ProfileManager* m_pm;
    FetchWorker* m_fetchWorker;
    QTimer* m_refreshTimer;
    QTimer* m_frameTimer;
    QTimer* m_reDockTimer;
    QPropertyAnimation* m_glowAnim;
    QParallelAnimationGroup* m_toggleGroup;
    QPropertyAnimation* m_fadeAnim;
    ContextMenu* m_menu;
};
