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
#include <cmath>

#include "ProfileManager.h"
#include "FetchThread.h"

// 预定义颜色常量
namespace Colors {
    inline QColor text() { return QColor(235, 237, 245); }
    inline QColor label() { return QColor(120, 125, 145); }
    inline QColor dim() { return QColor(80, 85, 100); }
    inline QColor accent() { return QColor(70, 140, 255); }
    inline QColor error() { return QColor(255, 100, 100); }
    inline QColor bg0() { return QColor(14, 14, 20, 220); }
    inline QColor bg1() { return QColor(18, 18, 26, 217); }
}

// SpringValue 弹簧动画值
class SpringValue {
public:
    SpringValue(double v = 0);
    
    void target(double t);
    bool tick(double dt = 0.016, double spring = 160, double damp = 10);
    
    double val() const { return m_cur; }

private:
    double m_cur;
    double m_tgt;
    double m_vel;
};

// ContextMenu 右键菜单
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

// DynamicIsland 主窗口
class DynamicIsland : public QMainWindow {
    Q_OBJECT
    Q_PROPERTY(double glowOffset READ glowOffset WRITE setGlowOffset)
    Q_PROPERTY(double itemOpacity READ itemOpacity WRITE setItemOpacity)

    friend class ContextMenu;

public:
    explicit DynamicIsland(QWidget* parent = nullptr);

    double glowOffset() const { return m_glowOffset; }
    void setGlowOffset(double v);
    
    double itemOpacity() const { return m_itemOpacity; }
    void setItemOpacity(double v);

public slots:
    void addCookie();
    void deleteCurrentCookie();
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

private slots:
    void onData(const FetchResult& result);
    void onTick();
    void onFadeIn();

private:
    void drawBg(QPainter& p, int w, int h);
    void drawGlow(QPainter& p, int w, int h);
    void drawBorder(QPainter& p, int w, int h);
    void drawFlowLight(QPainter& p, int w, int h);
    void drawCompact(QPainter& p, int w, int h);
    void drawExpanded(QPainter& p, int w, int h);
    void toggle();
    
    QFont fontUi(int size = 10, QFont::Weight weight = QFont::Normal);
    QFont fontMono(int size = 11, QFont::Weight weight = QFont::Medium);

    static constexpr int COMPACT_W = 168;
    static constexpr int COMPACT_H = 44;
    static constexpr int EXPANDED_W = 320;
    static constexpr int EXPANDED_H = 195;

    bool m_expanded = false;
    bool m_hovered = false;
    bool m_pinned = true;
    QPoint m_dragPos;
    bool m_dragMoved = false;
    double m_phase = 0.0;
    double m_itemOpacity = 1.0;
    QString m_errorMsg;
    int m_errorCode = 0;

    struct TokenData {
        double planUsed = 0;
        double planTotal = 0;
        double planPercent = 0;
        double compUsed = 0;
        double compTotal = 0;
        double compPercent = 0;
        double monthUsed = 0;
        double monthTotal = 0;
        double monthPercent = 0;
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
    QPropertyAnimation* m_glowAnim;
    QParallelAnimationGroup* m_toggleGroup;
    QPropertyAnimation* m_fadeAnim;
    ContextMenu* m_menu;
};
