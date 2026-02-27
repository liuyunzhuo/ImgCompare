#include "CompareWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

namespace {
inline bool isNativeYuv(PixelFormat fmt) {
    return fmt == PixelFormat::YUV420P || fmt == PixelFormat::YUV444P
        || fmt == PixelFormat::NV12 || fmt == PixelFormat::NV16;
}

inline int chromaWidth(const YuvPlanes& yuv) {
    return yuv.subsampling == ChromaSubsampling::Cs444 ? yuv.width : (yuv.width / 2);
}

inline int chromaHeight(const YuvPlanes& yuv) {
    return yuv.subsampling == ChromaSubsampling::Cs420 ? (yuv.height / 2) : yuv.height;
}

inline bool isValidYuv(const YuvPlanes& yuv) {
    if (yuv.width <= 0 || yuv.height <= 0) {
        return false;
    }
    const int ySize = yuv.width * yuv.height;
    const int cSize = chromaWidth(yuv) * chromaHeight(yuv);
    return yuv.y.size() >= ySize && yuv.u.size() >= cSize && yuv.v.size() >= cSize;
}

inline double psnrFromMse(double mse) {
    constexpr double peak = 255.0;
    return 10.0 * std::log10((peak * peak) / mse);
}

inline int samplePlaneMapped(const QByteArray& plane,
                             int planeW,
                             int planeH,
                             int frameW,
                             int frameH,
                             int x,
                             int y) {
    if (planeW <= 0 || planeH <= 0 || frameW <= 0 || frameH <= 0 || plane.isEmpty()) {
        return 0;
    }
    const int sx = qBound(0, (x * planeW) / frameW, planeW - 1);
    const int sy = qBound(0, (y * planeH) / frameH, planeH - 1);
    const int idx = sy * planeW + sx;
    return static_cast<int>(static_cast<unsigned char>(plane[idx]));
}
} // namespace

CompareWidget::CompareWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(640, 360);
}

void CompareWidget::setLeftImage(const LoadedImage& image) {
    m_leftImage = image;
    recomputePsnr();
    clampPanOffset();
    update();
}

void CompareWidget::setRightImage(const LoadedImage& image) {
    m_rightImage = image;
    recomputePsnr();
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

    if (!m_leftImage.image.isNull()) {
        const QRectF target = imageTargetRect(m_leftImage.image);
        p.save();
        p.setClipRect(QRect(0, 0, m_splitX, height()));
        p.drawImage(target, m_leftImage.image);
        p.restore();
    }

    if (!m_rightImage.image.isNull()) {
        const QRectF target = imageTargetRect(m_rightImage.image);
        p.save();
        p.setClipRect(QRect(m_splitX, 0, width() - m_splitX, height()));
        p.drawImage(target, m_rightImage.image);
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

    if (m_psnr.available) {
        const bool fullscreen = window() && window()->isFullScreen();
        const auto formatValue = [](double val, bool inf) -> QString {
            if (inf) {
                return "INF dB";
            }
            return QString::number(val, 'f', 2) + " dB";
        };

        const QString text = QString("PSNR\nY: %1\nU: %2\nV: %3\n%4x%5")
                                 .arg(formatValue(m_psnr.y, m_psnr.yInfinite))
                                 .arg(formatValue(m_psnr.u, m_psnr.uInfinite))
                                 .arg(formatValue(m_psnr.v, m_psnr.vInfinite))
                                 .arg(m_psnr.width)
                                 .arg(m_psnr.height);

        const int padding = 10;
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const QRect textRect = p.fontMetrics().boundingRect(
            QRect(0, 0, width(), height()),
            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
            text);
        const QRect panelRect(width() - textRect.width() - padding * 2 - 12,
                              12,
                              textRect.width() + padding * 2,
                              textRect.height() + padding * 2);

        p.setBrush(QColor(0, 0, 0, fullscreen ? 120 : 150));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(panelRect, 8, 8);

        p.setPen(QColor(245, 245, 245, 230));
        p.drawText(panelRect.adjusted(padding, padding, -padding, -padding),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   text);
    }
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

    intersectForImage(m_leftImage.image);
    intersectForImage(m_rightImage.image);

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

void CompareWidget::recomputePsnr() {
    m_psnr = PsnrMetrics{};
    if (!isValidYuv(m_leftImage.yuv) || !isValidYuv(m_rightImage.yuv)) {
        return;
    }

    const int w = qMin(m_leftImage.yuv.width, m_rightImage.yuv.width);
    const int h = qMin(m_leftImage.yuv.height, m_rightImage.yuv.height);
    if (w <= 0 || h <= 0) {
        return;
    }

    double seY = 0.0;
    double seU = 0.0;
    double seV = 0.0;
    double countY = 0.0;
    double countU = 0.0;
    double countV = 0.0;

    const bool directNativeMatch = isNativeYuv(m_leftImage.format) && isNativeYuv(m_rightImage.format)
        && m_leftImage.yuv.width == m_rightImage.yuv.width
        && m_leftImage.yuv.height == m_rightImage.yuv.height
        && m_leftImage.yuv.subsampling == m_rightImage.yuv.subsampling;

    if (directNativeMatch) {
        const int yCount = m_leftImage.yuv.width * m_leftImage.yuv.height;
        const int cCount = chromaWidth(m_leftImage.yuv) * chromaHeight(m_leftImage.yuv);
        const auto* ly = reinterpret_cast<const uchar*>(m_leftImage.yuv.y.constData());
        const auto* lu = reinterpret_cast<const uchar*>(m_leftImage.yuv.u.constData());
        const auto* lv = reinterpret_cast<const uchar*>(m_leftImage.yuv.v.constData());
        const auto* ry = reinterpret_cast<const uchar*>(m_rightImage.yuv.y.constData());
        const auto* ru = reinterpret_cast<const uchar*>(m_rightImage.yuv.u.constData());
        const auto* rv = reinterpret_cast<const uchar*>(m_rightImage.yuv.v.constData());

        for (int i = 0; i < yCount; ++i) {
            const double d = static_cast<double>(ly[i]) - static_cast<double>(ry[i]);
            seY += d * d;
        }
        for (int i = 0; i < cCount; ++i) {
            const double du = static_cast<double>(lu[i]) - static_cast<double>(ru[i]);
            const double dv = static_cast<double>(lv[i]) - static_cast<double>(rv[i]);
            seU += du * du;
            seV += dv * dv;
        }
        countY = static_cast<double>(yCount);
        countU = static_cast<double>(cCount);
        countV = static_cast<double>(cCount);
    } else {
        const int lCW = chromaWidth(m_leftImage.yuv);
        const int lCH = chromaHeight(m_leftImage.yuv);
        const int rCW = chromaWidth(m_rightImage.yuv);
        const int rCH = chromaHeight(m_rightImage.yuv);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int ly = samplePlaneMapped(m_leftImage.yuv.y,
                                                 m_leftImage.yuv.width,
                                                 m_leftImage.yuv.height,
                                                 w,
                                                 h,
                                                 x,
                                                 y);
                const int ry = samplePlaneMapped(m_rightImage.yuv.y,
                                                 m_rightImage.yuv.width,
                                                 m_rightImage.yuv.height,
                                                 w,
                                                 h,
                                                 x,
                                                 y);
                const int lu = samplePlaneMapped(m_leftImage.yuv.u, lCW, lCH, w, h, x, y);
                const int ru = samplePlaneMapped(m_rightImage.yuv.u, rCW, rCH, w, h, x, y);
                const int lv = samplePlaneMapped(m_leftImage.yuv.v, lCW, lCH, w, h, x, y);
                const int rv = samplePlaneMapped(m_rightImage.yuv.v, rCW, rCH, w, h, x, y);

                const double dy = static_cast<double>(ly - ry);
                const double du = static_cast<double>(lu - ru);
                const double dv = static_cast<double>(lv - rv);
                seY += dy * dy;
                seU += du * du;
                seV += dv * dv;
            }
        }
        const double total = static_cast<double>(w) * static_cast<double>(h);
        countY = total;
        countU = total;
        countV = total;
    }

    const double mseY = countY > 0.0 ? (seY / countY) : 0.0;
    const double mseU = countU > 0.0 ? (seU / countU) : 0.0;
    const double mseV = countV > 0.0 ? (seV / countV) : 0.0;

    m_psnr.available = true;
    m_psnr.width = w;
    m_psnr.height = h;
    m_psnr.yInfinite = mseY <= 0.0;
    m_psnr.uInfinite = mseU <= 0.0;
    m_psnr.vInfinite = mseV <= 0.0;
    m_psnr.y = m_psnr.yInfinite ? 0.0 : psnrFromMse(mseY);
    m_psnr.u = m_psnr.uInfinite ? 0.0 : psnrFromMse(mseU);
    m_psnr.v = m_psnr.vInfinite ? 0.0 : psnrFromMse(mseV);
}
