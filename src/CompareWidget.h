#pragma once

#include "ImageLoader.h"

#include <QImage>
#include <QPointF>
#include <QWidget>

class QMouseEvent;
class QResizeEvent;
class QWheelEvent;
class QEvent;
class QKeyEvent;

class CompareWidget : public QWidget {
    Q_OBJECT
public:
    explicit CompareWidget(QWidget* parent = nullptr);

    void setLeftImage(const LoadedImage& image);
    void setRightImage(const LoadedImage& image);
    void setShowPixelInfo(bool enabled);
    void setShowPsnr(bool enabled);
    void setShowPixelDiff(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct PsnrMetrics {
        bool available = false;
        int width = 0;
        int height = 0;
        double y = 0.0;
        double u = 0.0;
        double v = 0.0;
        bool yInfinite = false;
        bool uInfinite = false;
        bool vInfinite = false;
    };

    struct PanBounds {
        qreal minX = 0.0;
        qreal maxX = 0.0;
        qreal minY = 0.0;
        qreal maxY = 0.0;
    };

    struct CursorSample {
        bool valid = false;
        bool anchorLeftSide = true;
        int anchorX = 0;
        int anchorY = 0;

        bool leftValid = false;
        int leftX = 0;
        int leftY = 0;
        int leftYVal = 0;
        int leftUVal = 0;
        int leftVVal = 0;

        bool rightValid = false;
        int rightX = 0;
        int rightY = 0;
        int rightYVal = 0;
        int rightUVal = 0;
        int rightVVal = 0;
    };

    QRectF imageTargetRect(const QImage& image) const;
    bool hitHandle(const QPoint& pos) const;
    void setSplitX(int x);
    void resetView();
    PanBounds calcPanBounds() const;
    void clampPanOffset();
    void recomputePsnr();
    void updateCursorSample(const QPoint& pos);
    bool sampleAtWidgetPos(const LoadedImage& image, const QPoint& pos, bool leftSide, CursorSample& out) const;
    bool sampleAtImagePos(const LoadedImage& image, int x, int y, bool leftSide, CursorSample& out) const;
    void rebuildCursorSampleFromAnchor();
    void moveCursorSampleBy(int dx, int dy);

    LoadedImage m_leftImage;
    LoadedImage m_rightImage;
    PsnrMetrics m_psnr;
    CursorSample m_cursorSample;
    int m_splitX = -1;
    bool m_showPixelInfo = true;
    bool m_showPsnr = true;
    bool m_showPixelDiff = true;
    bool m_dragging = false;
    bool m_panning = false;
    QPoint m_lastPanPos;
    double m_zoom = 1.0;
    QPointF m_panOffset{0.0, 0.0};
};
