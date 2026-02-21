#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

class QMouseEvent;
class QResizeEvent;
class QWheelEvent;

class CompareWidget : public QWidget {
    Q_OBJECT
public:
    explicit CompareWidget(QWidget* parent = nullptr);

    void setLeftImage(const QImage& image);
    void setRightImage(const QImage& image);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct PanBounds {
        qreal minX = 0.0;
        qreal maxX = 0.0;
        qreal minY = 0.0;
        qreal maxY = 0.0;
    };

    QRectF imageTargetRect(const QImage& image) const;
    bool hitHandle(const QPoint& pos) const;
    void setSplitX(int x);
    void resetView();
    PanBounds calcPanBounds() const;
    void clampPanOffset();

    QImage m_leftImage;
    QImage m_rightImage;
    int m_splitX = -1;
    bool m_dragging = false;
    bool m_panning = false;
    QPoint m_lastPanPos;
    double m_zoom = 1.0;
    QPointF m_panOffset{0.0, 0.0};
};
