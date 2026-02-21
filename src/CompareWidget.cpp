#include "CompareWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

CompareWidget::CompareWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(640, 360);
}

void CompareWidget::setLeftImage(const QImage& image) {
    m_leftImage = image;
    clampPanOffset();
    update();
}

void CompareWidget::setRightImage(const QImage& image) {
    m_rightImage = image;
    clampPanOffset();
    update();
}

QRectF CompareWidget::imageTargetRect(const QImage& image) const {
    if (image.isNull()) {
        return rect();
    }
    const QSize base = image.size().scaled(size(), Qt::KeepAspectRatio);
    const QSizeF fitted(base.width() * m_zoom, base.height() * m_zoom);
    const qreal x = (width() - fitted.width()) / 2.0 + m_panOffset.x();
    const qreal y = (height() - fitted.height()) / 2.0 + m_panOffset.y();
    return QRectF(x, y, fitted.width(), fitted.height());
}

void CompareWidget::setSplitX(int x) {
    m_splitX = qBound(0, x, width());
    update();
}

bool CompareWidget::hitHandle(const QPoint& pos) const {
    if (m_splitX < 0) {
        return false;
    }
    const QRect handleRect(m_splitX - 16, height() / 2 - 24, 32, 48);
    return handleRect.adjusted(-4, -4, 4, 4).contains(pos);
}

void CompareWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 20));

    if (m_splitX < 0) {
        m_splitX = width() / 2;
    }

    if (!m_leftImage.isNull()) {
        const QRectF target = imageTargetRect(m_leftImage);
        p.save();
        p.setClipRect(QRect(0, 0, m_splitX, height()));
        p.drawImage(target, m_leftImage);
        p.restore();
    }

    if (!m_rightImage.isNull()) {
        const QRectF target = imageTargetRect(m_rightImage);
        p.save();
        p.setClipRect(QRect(m_splitX, 0, width() - m_splitX, height()));
        p.drawImage(target, m_rightImage);
        p.restore();
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(255, 255, 255, 220), 2));
    p.drawLine(m_splitX, 0, m_splitX, height());

    const QRect handleRect(m_splitX - 14, height() / 2 - 22, 28, 44);
    p.setBrush(QColor(255, 255, 255, 230));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(handleRect, 10, 10);

    p.setPen(QPen(QColor(40, 40, 40), 2));
    p.drawLine(handleRect.center().x() - 4, handleRect.center().y() - 8,
               handleRect.center().x() - 4, handleRect.center().y() + 8);
    p.drawLine(handleRect.center().x() + 4, handleRect.center().y() - 8,
               handleRect.center().x() + 4, handleRect.center().y() + 8);
}

void CompareWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && (hitHandle(event->pos()) || qAbs(event->pos().x() - m_splitX) < 8)) {
        m_dragging = true;
        setSplitX(event->pos().x());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CompareWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPos;
        m_lastPanPos = event->pos();
        m_panOffset += QPointF(delta.x(), delta.y());
        clampPanOffset();
        update();
        event->accept();
        return;
    }

    if (m_dragging) {
        setSplitX(event->pos().x());
        event->accept();
        return;
    }
    if (hitHandle(event->pos()) || qAbs(event->pos().x() - m_splitX) < 8) {
        setCursor(Qt::SizeHorCursor);
    } else {
        unsetCursor();
    }
    QWidget::mouseMoveEvent(event);
}

void CompareWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_panning = false;
        unsetCursor();
    }
    QWidget::mouseReleaseEvent(event);
}

void CompareWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        resetView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CompareWidget::wheelEvent(QWheelEvent* event) {
    const QPoint numDegrees = event->angleDelta();
    if (numDegrees.y() == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    const double oldZoom = m_zoom;
    const double factor = numDegrees.y() > 0 ? 1.12 : (1.0 / 1.12);
    m_zoom = qBound(0.1, m_zoom * factor, 20.0);

    const QPointF cursorPos = event->position();
    // Keep the pixel under the cursor stable while zooming.
    m_panOffset = cursorPos - (cursorPos - m_panOffset) * (m_zoom / oldZoom);
    clampPanOffset();

    update();
    event->accept();
}

void CompareWidget::resizeEvent(QResizeEvent* event) {
    if (m_splitX < 0) {
        m_splitX = width() / 2;
    } else {
        m_splitX = qMin(m_splitX, width());
    }
    clampPanOffset();
    QWidget::resizeEvent(event);
}

void CompareWidget::resetView() {
    m_zoom = 1.0;
    m_panOffset = QPointF(0.0, 0.0);
    clampPanOffset();
    update();
}

CompareWidget::PanBounds CompareWidget::calcPanBounds() const {
    PanBounds bounds;
    bool hasAnyImage = false;

    auto axisRange = [](qreal content, qreal viewport, qreal& minVal, qreal& maxVal) {
        if (content <= viewport) {
            minVal = 0.0;
            maxVal = 0.0;
            return;
        }
        const qreal d = (content - viewport) * 0.5;
        minVal = -d;
        maxVal = d;
    };

    auto intersectForImage = [&](const QImage& image) {
        if (image.isNull()) {
            return;
        }

        const QSize base = image.size().scaled(size(), Qt::KeepAspectRatio);
        const qreal contentW = base.width() * m_zoom;
        const qreal contentH = base.height() * m_zoom;

        qreal minX = 0.0;
        qreal maxX = 0.0;
        qreal minY = 0.0;
        qreal maxY = 0.0;
        axisRange(contentW, width(), minX, maxX);
        axisRange(contentH, height(), minY, maxY);

        if (!hasAnyImage) {
            bounds.minX = minX;
            bounds.maxX = maxX;
            bounds.minY = minY;
            bounds.maxY = maxY;
            hasAnyImage = true;
            return;
        }

        bounds.minX = qMax(bounds.minX, minX);
        bounds.maxX = qMin(bounds.maxX, maxX);
        bounds.minY = qMax(bounds.minY, minY);
        bounds.maxY = qMin(bounds.maxY, maxY);
    };

    intersectForImage(m_leftImage);
    intersectForImage(m_rightImage);

    if (!hasAnyImage || bounds.minX > bounds.maxX || bounds.minY > bounds.maxY) {
        return PanBounds{};
    }
    return bounds;
}

void CompareWidget::clampPanOffset() {
    const PanBounds bounds = calcPanBounds();
    m_panOffset.setX(qBound(bounds.minX, m_panOffset.x(), bounds.maxX));
    m_panOffset.setY(qBound(bounds.minY, m_panOffset.y(), bounds.maxY));
}
