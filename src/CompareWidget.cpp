#include "CompareWidget.h"

#include <QEvent>
#include <QKeyEvent>
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
    setFocusPolicy(Qt::StrongFocus);
}

void CompareWidget::setLeftImage(const LoadedImage& image) {
    m_leftImage = image;
    recomputePsnr();
    m_cursorSample.valid = false;
    clampPanOffset();
    update();
}

void CompareWidget::setRightImage(const LoadedImage& image) {
    m_rightImage = image;
    recomputePsnr();
    m_cursorSample.valid = false;
    clampPanOffset();
    update();
}

void CompareWidget::setShowPixelInfo(bool enabled) {
    if (m_showPixelInfo == enabled) {
        return;
    }
    m_showPixelInfo = enabled;
    if (!m_showPixelInfo) {
        m_cursorSample.valid = false;
    }
    update();
}

void CompareWidget::setShowPsnr(bool enabled) {
    if (m_showPsnr == enabled) {
        return;
    }
    m_showPsnr = enabled;
    update();
}

void CompareWidget::setShowPixelDiff(bool enabled) {
    if (m_showPixelDiff == enabled) {
        return;
    }
    m_showPixelDiff = enabled;
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

    int topOffset = 12;
    if (m_showPsnr && m_psnr.available) {
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
                              topOffset,
                              textRect.width() + padding * 2,
                              textRect.height() + padding * 2);

        p.setBrush(QColor(0, 0, 0, fullscreen ? 120 : 150));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(panelRect, 8, 8);

        p.setPen(QColor(245, 245, 245, 230));
        p.drawText(panelRect.adjusted(padding, padding, -padding, -padding),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   text);
        topOffset = panelRect.bottom() + 10;
    }

    if (m_showPixelInfo && m_cursorSample.valid) {
        QString text = QString("Anchor: %1 (%2, %3)")
                           .arg(m_cursorSample.anchorLeftSide ? "Left" : "Right")
                           .arg(m_cursorSample.anchorX)
                           .arg(m_cursorSample.anchorY);
        if (m_cursorSample.leftValid) {
            text += QString("\nL (%1, %2)  Y:%3 U:%4 V:%5")
                        .arg(m_cursorSample.leftX)
                        .arg(m_cursorSample.leftY)
                        .arg(m_cursorSample.leftYVal)
                        .arg(m_cursorSample.leftUVal)
                        .arg(m_cursorSample.leftVVal);
        } else {
            text += "\nL: N/A";
        }
        if (m_cursorSample.rightValid) {
            text += QString("\nR (%1, %2)  Y:%3 U:%4 V:%5")
                        .arg(m_cursorSample.rightX)
                        .arg(m_cursorSample.rightY)
                        .arg(m_cursorSample.rightYVal)
                        .arg(m_cursorSample.rightUVal)
                        .arg(m_cursorSample.rightVVal);
        } else {
            text += "\nR: N/A";
        }
        if (m_showPixelDiff && m_cursorSample.leftValid && m_cursorSample.rightValid) {
            text += QString("\nDiff(YUV): dY=%1 dU=%2 dV=%3")
                        .arg(m_cursorSample.leftYVal - m_cursorSample.rightYVal)
                        .arg(m_cursorSample.leftUVal - m_cursorSample.rightUVal)
                        .arg(m_cursorSample.leftVVal - m_cursorSample.rightVVal);
        }

        const int padding = 10;
        const QRect textRect = p.fontMetrics().boundingRect(
            QRect(0, 0, width(), height()),
            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
            text);
        const QRect panelRect(width() - textRect.width() - padding * 2 - 12,
                              topOffset,
                              textRect.width() + padding * 2,
                              textRect.height() + padding * 2);

        p.setBrush(QColor(0, 0, 0, 140));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(panelRect, 8, 8);
        p.setPen(QColor(245, 245, 245, 230));
        p.drawText(panelRect.adjusted(padding, padding, -padding, -padding),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   text);

        const auto drawPixelBox = [&](const LoadedImage& image, int px, int py, bool leftSide, bool anchorSide) {
            if (image.image.isNull()) {
                return;
            }
            const QRectF target = imageTargetRect(image.image);
            const qreal pixelW = target.width() / image.yuv.width;
            const qreal pixelH = target.height() / image.yuv.height;
            if (pixelW < 6.0 || pixelH < 6.0) {
                return;
            }
            const QRectF pixelRect(target.left() + px * pixelW,
                                   target.top() + py * pixelH,
                                   pixelW,
                                   pixelH);
            p.save();
            if (leftSide) {
                p.setClipRect(QRect(0, 0, m_splitX, height()));
            } else {
                p.setClipRect(QRect(m_splitX, 0, width() - m_splitX, height()));
            }
            p.setPen(QPen(QColor(255, 255, 255, anchorSide ? 250 : 210), anchorSide ? 1.8 : 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(pixelRect);
            p.restore();
        };
        if (m_cursorSample.leftValid) {
            drawPixelBox(m_leftImage, m_cursorSample.leftX, m_cursorSample.leftY, true, m_cursorSample.anchorLeftSide);
        }
        if (m_cursorSample.rightValid) {
            drawPixelBox(m_rightImage, m_cursorSample.rightX, m_cursorSample.rightY, false, !m_cursorSample.anchorLeftSide);
        }
    }
}

void CompareWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
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
    setFocus(Qt::MouseFocusReason);
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

    updateCursorSample(event->pos());
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

void CompareWidget::leaveEvent(QEvent* event) {
    m_cursorSample.valid = false;
    update();
    QWidget::leaveEvent(event);
}

void CompareWidget::keyPressEvent(QKeyEvent* event) {
    if (!m_showPixelInfo || !m_cursorSample.valid) {
        QWidget::keyPressEvent(event);
        return;
    }

    int dx = 0;
    int dy = 0;
    switch (event->key()) {
    case Qt::Key_W:
        dy = -1;
        break;
    case Qt::Key_S:
        dy = 1;
        break;
    case Qt::Key_A:
        dx = -1;
        break;
    case Qt::Key_D:
        dx = 1;
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    moveCursorSampleBy(dx, dy);
    event->accept();
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

void CompareWidget::updateCursorSample(const QPoint& pos) {
    if (!m_showPixelInfo) {
        return;
    }

    CursorSample sample;
    if (sampleAtWidgetPos(m_leftImage, pos, true, sample) || sampleAtWidgetPos(m_rightImage, pos, false, sample)) {
        m_cursorSample.valid = true;
        m_cursorSample.anchorLeftSide = sample.anchorLeftSide;
        m_cursorSample.anchorX = sample.anchorX;
        m_cursorSample.anchorY = sample.anchorY;
        rebuildCursorSampleFromAnchor();
    } else {
        m_cursorSample.valid = false;
    }
    update();
}

bool CompareWidget::sampleAtWidgetPos(const LoadedImage& image, const QPoint& pos, bool leftSide, CursorSample& out) const {
    if (image.image.isNull()) {
        return false;
    }
    if (leftSide && pos.x() > m_splitX) {
        return false;
    }
    if (!leftSide && pos.x() < m_splitX) {
        return false;
    }

    const QRectF target = imageTargetRect(image.image);
    if (!target.contains(pos)) {
        return false;
    }
    if (!isValidYuv(image.yuv)) {
        return false;
    }

    const int x = qBound(0, static_cast<int>((pos.x() - target.left()) * image.yuv.width / target.width()), image.yuv.width - 1);
    const int y = qBound(0, static_cast<int>((pos.y() - target.top()) * image.yuv.height / target.height()), image.yuv.height - 1);
    out.valid = true;
    out.anchorLeftSide = leftSide;
    out.anchorX = x;
    out.anchorY = y;
    return true;
}

bool CompareWidget::sampleAtImagePos(const LoadedImage& image, int x, int y, bool leftSide, CursorSample& out) const {
    if (!isValidYuv(image.yuv)) {
        return false;
    }
    x = qBound(0, x, image.yuv.width - 1);
    y = qBound(0, y, image.yuv.height - 1);
    const int yIdx = y * image.yuv.width + x;
    const int cW = chromaWidth(image.yuv);
    const int cH = chromaHeight(image.yuv);
    const int cx = image.yuv.subsampling == ChromaSubsampling::Cs444 ? x : (x / 2);
    const int cy = image.yuv.subsampling == ChromaSubsampling::Cs420 ? (y / 2) : y;
    const int cIdx = qBound(0, cy * cW + cx, cW * cH - 1);

    out.valid = true;
    if (leftSide) {
        out.leftValid = true;
        out.leftX = x;
        out.leftY = y;
        out.leftYVal = static_cast<int>(static_cast<unsigned char>(image.yuv.y[yIdx]));
        out.leftUVal = static_cast<int>(static_cast<unsigned char>(image.yuv.u[cIdx]));
        out.leftVVal = static_cast<int>(static_cast<unsigned char>(image.yuv.v[cIdx]));
    } else {
        out.rightValid = true;
        out.rightX = x;
        out.rightY = y;
        out.rightYVal = static_cast<int>(static_cast<unsigned char>(image.yuv.y[yIdx]));
        out.rightUVal = static_cast<int>(static_cast<unsigned char>(image.yuv.u[cIdx]));
        out.rightVVal = static_cast<int>(static_cast<unsigned char>(image.yuv.v[cIdx]));
    }
    return true;
}

void CompareWidget::rebuildCursorSampleFromAnchor() {
    if (!m_cursorSample.valid) {
        return;
    }

    CursorSample rebuilt;
    rebuilt.valid = true;
    rebuilt.anchorLeftSide = m_cursorSample.anchorLeftSide;
    rebuilt.anchorX = m_cursorSample.anchorX;
    rebuilt.anchorY = m_cursorSample.anchorY;

    if (m_cursorSample.anchorLeftSide) {
        if (isValidYuv(m_leftImage.yuv)) {
            sampleAtImagePos(m_leftImage, m_cursorSample.anchorX, m_cursorSample.anchorY, true, rebuilt);
        }
        if (isValidYuv(m_rightImage.yuv) && isValidYuv(m_leftImage.yuv)) {
            const int rx = (m_leftImage.yuv.width > 1 && m_rightImage.yuv.width > 1)
                               ? qRound((static_cast<double>(m_cursorSample.anchorX) * (m_rightImage.yuv.width - 1)) / (m_leftImage.yuv.width - 1))
                               : 0;
            const int ry = (m_leftImage.yuv.height > 1 && m_rightImage.yuv.height > 1)
                               ? qRound((static_cast<double>(m_cursorSample.anchorY) * (m_rightImage.yuv.height - 1)) / (m_leftImage.yuv.height - 1))
                               : 0;
            sampleAtImagePos(m_rightImage, rx, ry, false, rebuilt);
        }
    } else {
        if (isValidYuv(m_rightImage.yuv)) {
            sampleAtImagePos(m_rightImage, m_cursorSample.anchorX, m_cursorSample.anchorY, false, rebuilt);
        }
        if (isValidYuv(m_leftImage.yuv) && isValidYuv(m_rightImage.yuv)) {
            const int lx = (m_rightImage.yuv.width > 1 && m_leftImage.yuv.width > 1)
                               ? qRound((static_cast<double>(m_cursorSample.anchorX) * (m_leftImage.yuv.width - 1)) / (m_rightImage.yuv.width - 1))
                               : 0;
            const int ly = (m_rightImage.yuv.height > 1 && m_leftImage.yuv.height > 1)
                               ? qRound((static_cast<double>(m_cursorSample.anchorY) * (m_leftImage.yuv.height - 1)) / (m_rightImage.yuv.height - 1))
                               : 0;
            sampleAtImagePos(m_leftImage, lx, ly, true, rebuilt);
        }
    }

    m_cursorSample = rebuilt;
}

void CompareWidget::moveCursorSampleBy(int dx, int dy) {
    if (!m_cursorSample.valid) {
        return;
    }
    const LoadedImage& anchorImg = m_cursorSample.anchorLeftSide ? m_leftImage : m_rightImage;
    if (!isValidYuv(anchorImg.yuv)) {
        return;
    }
    m_cursorSample.anchorX = qBound(0, m_cursorSample.anchorX + dx, anchorImg.yuv.width - 1);
    m_cursorSample.anchorY = qBound(0, m_cursorSample.anchorY + dy, anchorImg.yuv.height - 1);
    rebuildCursorSampleFromAnchor();
    update();
}
