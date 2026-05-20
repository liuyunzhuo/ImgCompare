#include "MultiCompareWidget.h"

#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QEvent>

namespace {
constexpr int kOuterPadding = 12;
constexpr int kColumnSpacing = 12;
constexpr int kInnerPadding = 8;
constexpr int kLabelGap = 6;
constexpr int kMinColumnWidth = 160;
constexpr int kHandleRadius = 8;
constexpr qreal kMinViewportSize = 0.2;
constexpr int kRemoveButtonSize = 18;
}

MultiCompareWidget::MultiCompareWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(640, 360);
    setFocusPolicy(Qt::StrongFocus);

    m_titleEditor = new QLineEdit(this);
    QFont editorFont = font();
    editorFont.setPointSize(editorFont.pointSize() + 2);
    m_titleEditor->setFont(editorFont);
    m_titleEditor->hide();
    connect(m_titleEditor, &QLineEdit::editingFinished, this, &MultiCompareWidget::commitInlineTitleEdit);
}

void MultiCompareWidget::setItems(const QVector<Item>& items) {
    m_items = items;
    m_hoveredRemoveIndex = -1;
    cancelInlineTitleEdit();
    resetView();
}

void MultiCompareWidget::setItemLabel(int index, const QString& label) {
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    m_items[index].label = label;
    update();
}

void MultiCompareWidget::clearItems() {
    cancelInlineTitleEdit();
    m_hoveredRemoveIndex = -1;
    m_items.clear();
    resetView();
}

bool MultiCompareWidget::hasItems() const {
    return !m_items.isEmpty();
}

QImage MultiCompareWidget::renderComparisonImage(qreal devicePixelRatio) const {
    QRect target = rect();
    if (target.width() <= 0 || target.height() <= 0) {
        target = QRect(0, 0, 1280, 720);
    }

    const QSize pixelSize(qMax(1, qRound(target.width() * devicePixelRatio)),
                          qMax(1, qRound(target.height() * devicePixelRatio)));
    QImage out(pixelSize, QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(devicePixelRatio);
    out.fill(QColor(20, 20, 20));

    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    drawScene(painter, QRect(QPoint(0, 0), target.size()));

    const QRect contentRect = exportContentRect(QRect(QPoint(0, 0), target.size()));
    const QRect pixelCrop(qMax(0, qRound(contentRect.x() * devicePixelRatio)),
                          qMax(0, qRound(contentRect.y() * devicePixelRatio)),
                          qMax(1, qRound(contentRect.width() * devicePixelRatio)),
                          qMax(1, qRound(contentRect.height() * devicePixelRatio)));
    QImage cropped = out.copy(pixelCrop.intersected(out.rect()));
    cropped.setDevicePixelRatio(devicePixelRatio);
    return cropped;
}

void MultiCompareWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    drawScene(painter, rect());
    drawViewportOverlay(painter);
}

void MultiCompareWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    m_hoveredColumn = columnIndexAt(event->pos());
    m_hoveredRemoveIndex = -1;

    if (event->button() == Qt::LeftButton) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (removeButtonRect(columnLayout(i)).contains(event->pos())) {
                emit itemRemoveRequested(i);
                event->accept();
                return;
            }
        }
    }

    if (event->button() == Qt::LeftButton) {
        m_viewportHandle = viewportHandleAt(event->pos());
        if (m_viewportHandle != HandleNone) {
            m_dragMode = DragMode::ResizeViewport;
            m_lastMousePos = event->pos();
            m_dragAnchorViewportRect = m_viewportRectNormalized;
            updateCursorForPosition(event->pos());
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton) {
        m_dragMode = DragMode::Pan;
        m_activeColumn = m_hoveredColumn;
        m_lastMousePos = event->pos();
        m_dragAnchorFocus = m_focus;
        updateCursorForPosition(event->pos());
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void MultiCompareWidget::mouseMoveEvent(QMouseEvent* event) {
    m_hoveredColumn = columnIndexAt(event->pos());
    m_hoveredRemoveIndex = -1;
    if (m_dragMode == DragMode::None) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (removeButtonRect(columnLayout(i)).contains(event->pos())) {
                m_hoveredRemoveIndex = i;
                break;
            }
        }
    }

    if (m_dragMode == DragMode::ResizeViewport) {
        const int referenceColumn = qMax(0, (m_activeColumn >= 0) ? m_activeColumn : m_hoveredColumn);
        const ColumnLayout layout = columnLayout(referenceColumn);
        if (layout.availableRect.width() > 0 && layout.availableRect.height() > 0) {
            const QPoint delta = event->pos() - m_lastMousePos;
            QRectF next = m_dragAnchorViewportRect;
            const qreal dx = static_cast<qreal>(delta.x()) / layout.availableRect.width();
            const qreal dy = static_cast<qreal>(delta.y()) / layout.availableRect.height();

            if (m_viewportHandle & HandleLeft) {
                next.setLeft(next.left() + dx);
            } else if (m_viewportHandle & HandleRight) {
                next.setRight(next.right() + dx);
            }

            if (m_viewportHandle & HandleTop) {
                next.setTop(next.top() + dy);
            } else if (m_viewportHandle & HandleBottom) {
                next.setBottom(next.bottom() + dy);
            }

            m_viewportRectNormalized = next.normalized();
            clampViewportRect();
            clampFocus();
            update();
        }
        event->accept();
        return;
    }

    if (m_dragMode == DragMode::Pan) {
        const int referenceColumn = (m_activeColumn >= 0) ? m_activeColumn : m_hoveredColumn;
        if (referenceColumn >= 0 && referenceColumn < m_items.size()) {
            const ColumnLayout layout = columnLayout(referenceColumn);
            const QImage& image = m_items[referenceColumn].image.image;
            if (!image.isNull() && layout.availableRect.width() > 0 && layout.availableRect.height() > 0) {
                const QPoint delta = event->pos() - m_lastMousePos;
                const QSize base = image.size().scaled(layout.availableRect.size(), Qt::KeepAspectRatio);
                const qreal scaledW = base.width() * m_zoom;
                const qreal scaledH = base.height() * m_zoom;
                if (scaledW > 0.0) {
                    m_focus.setX(m_dragAnchorFocus.x() - static_cast<qreal>(delta.x()) / scaledW);
                }
                if (scaledH > 0.0) {
                    m_focus.setY(m_dragAnchorFocus.y() - static_cast<qreal>(delta.y()) / scaledH);
                }
                clampFocus();
                update();
            }
        }
        event->accept();
        return;
    }

    updateCursorForPosition(event->pos());
    update();
    QWidget::mouseMoveEvent(event);
}

void MultiCompareWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_dragMode = DragMode::None;
        m_viewportHandle = HandleNone;
        m_activeColumn = -1;
        m_hoveredColumn = columnIndexAt(event->pos());
        m_hoveredRemoveIndex = -1;
        updateCursorForPosition(event->pos());
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void MultiCompareWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        for (int i = 0; i < m_items.size(); ++i) {
            const ColumnLayout layout = columnLayout(i);
            if (titleRect(layout).contains(event->pos())) {
                beginInlineTitleEdit(i);
                event->accept();
                return;
            }
        }
    }

    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        resetView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MultiCompareWidget::wheelEvent(QWheelEvent* event) {
    if (m_items.isEmpty() || event->angleDelta().y() == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    const int referenceColumn = columnIndexAt(event->position().toPoint());
    const double oldZoom = m_zoom;
    const double factor = event->angleDelta().y() > 0 ? 1.12 : (1.0 / 1.12);
    m_zoom = qBound(0.1, m_zoom * factor, 20.0);

    if (!qFuzzyCompare(oldZoom, m_zoom) && referenceColumn >= 0 && referenceColumn < m_items.size()) {
        bool ok = false;
        const QPointF before = widgetPosToNormalized(event->position(), referenceColumn, &ok);
        if (ok) {
            const ColumnLayout layout = columnLayout(referenceColumn);
            const QImage& image = m_items[referenceColumn].image.image;
            if (!image.isNull()) {
                const QSize base = image.size().scaled(layout.availableRect.size(), Qt::KeepAspectRatio);
                const qreal scaledW = base.width() * m_zoom;
                const qreal scaledH = base.height() * m_zoom;
                const QPointF viewportCenter = layout.availableRect.center();
                if (scaledW > 0.0) {
                    m_focus.setX(before.x() - (event->position().x() - viewportCenter.x()) / scaledW);
                }
                if (scaledH > 0.0) {
                    m_focus.setY(before.y() - (event->position().y() - viewportCenter.y()) / scaledH);
                }
            }
        }
    }

    clampFocus();
    update();
    event->accept();
}

void MultiCompareWidget::resizeEvent(QResizeEvent* event) {
    clampViewportRect();
    clampFocus();
    if (m_editingTitleIndex >= 0 && m_editingTitleIndex < m_items.size()) {
        beginInlineTitleEdit(m_editingTitleIndex);
    }
    QWidget::resizeEvent(event);
}

void MultiCompareWidget::leaveEvent(QEvent* event) {
    if (m_dragMode == DragMode::None) {
        m_hoveredColumn = -1;
        m_hoveredRemoveIndex = -1;
        unsetCursor();
        update();
    }
    QWidget::leaveEvent(event);
}

MultiCompareWidget::LayoutMetrics MultiCompareWidget::layoutMetrics() const {
    LayoutMetrics metrics;
    metrics.contentRect = rect().adjusted(kOuterPadding, kOuterPadding, -kOuterPadding, -kOuterPadding);
    metrics.spacing = kColumnSpacing;
    metrics.labelHeight = fontMetrics().height() + kInnerPadding * 2;
    if (m_items.isEmpty() || metrics.contentRect.width() <= 0 || metrics.contentRect.height() <= 0) {
        return metrics;
    }

    const int available = metrics.contentRect.width() + metrics.spacing;
    metrics.columns = qMax(1, available / (kMinColumnWidth + metrics.spacing));
    metrics.columns = qMin(metrics.columns, m_items.size());
    return metrics;
}

MultiCompareWidget::ColumnLayout MultiCompareWidget::columnLayout(int index) const {
    ColumnLayout layout;
    layout.index = index;

    const LayoutMetrics metrics = layoutMetrics();
    if (metrics.columns <= 0 || index < 0 || index >= m_items.size()) {
        return layout;
    }

    const int totalSpacing = (metrics.columns - 1) * metrics.spacing;
    const int columnWidth = qMax(1, (metrics.contentRect.width() - totalSpacing) / metrics.columns);
    const int extra = qMax(0, metrics.contentRect.width() - totalSpacing - columnWidth * metrics.columns);
    const int x = metrics.contentRect.left() + index * (columnWidth + metrics.spacing) + qMin(index, extra);
    const int width = columnWidth + (index < extra ? 1 : 0);

    layout.outerRect = QRect(x, metrics.contentRect.top(), width, metrics.contentRect.height());
    layout.availableRect = layout.outerRect.adjusted(kInnerPadding,
                                                     metrics.labelHeight + kLabelGap,
                                                     -kInnerPadding,
                                                     -kInnerPadding);
    layout.imageRect = viewportRectFromAvailable(layout.availableRect);
    return layout;
}

QRect MultiCompareWidget::viewportRectFromAvailable(const QRect& availableRect) const {
    const int left = availableRect.left() + qRound(m_viewportRectNormalized.left() * availableRect.width());
    const int top = availableRect.top() + qRound(m_viewportRectNormalized.top() * availableRect.height());
    const int right = availableRect.left() + qRound(m_viewportRectNormalized.right() * availableRect.width());
    const int bottom = availableRect.top() + qRound(m_viewportRectNormalized.bottom() * availableRect.height());
    return QRect(QPoint(left, top), QPoint(right, bottom)).normalized().intersected(availableRect);
}

QRectF MultiCompareWidget::imageDrawRect(const LoadedImage& image, const QRect& availableRect) const {
    if (image.image.isNull() || availableRect.width() <= 0 || availableRect.height() <= 0) {
        return QRectF(availableRect);
    }

    const QSize base = image.image.size().scaled(availableRect.size(), Qt::KeepAspectRatio);
    const QSizeF scaled(base.width() * m_zoom, base.height() * m_zoom);
    const QPointF availableCenter = availableRect.center();
    return QRectF(availableCenter.x() - m_focus.x() * scaled.width(),
                  availableCenter.y() - m_focus.y() * scaled.height(),
                  scaled.width(),
                  scaled.height());
}

QRect MultiCompareWidget::titleRect(const ColumnLayout& layout) const {
    const LayoutMetrics metrics = layoutMetrics();
    return QRect(layout.outerRect.left() + kInnerPadding,
                 layout.outerRect.top() + kInnerPadding,
                 layout.outerRect.width() - kInnerPadding * 2 - kRemoveButtonSize - 6,
                 metrics.labelHeight - kInnerPadding);
}

QRect MultiCompareWidget::removeButtonRect(const ColumnLayout& layout) const {
    const QRect title = titleRect(layout);
    const int x = layout.outerRect.right() - kInnerPadding - kRemoveButtonSize;
    const int y = title.center().y() - kRemoveButtonSize / 2;
    return QRect(x, y, kRemoveButtonSize, kRemoveButtonSize);
}

QPointF MultiCompareWidget::widgetPosToNormalized(const QPointF& pos, int columnIndex, bool* ok) const {
    if (ok) {
        *ok = false;
    }
    if (m_items.isEmpty() || columnIndex < 0 || columnIndex >= m_items.size()) {
        return {};
    }

    const ColumnLayout layout = columnLayout(columnIndex);
    const QRectF drawRect = imageDrawRect(m_items[columnIndex].image, layout.availableRect);
    if (drawRect.width() <= 0.0 || drawRect.height() <= 0.0 || !drawRect.contains(pos)) {
        return {};
    }

    if (ok) {
        *ok = true;
    }
    return QPointF((pos.x() - drawRect.left()) / drawRect.width(),
                   (pos.y() - drawRect.top()) / drawRect.height());
}

int MultiCompareWidget::viewportHandleAt(const QPoint& pos) const {
    const int columnIndex = columnIndexAt(pos);
    if (columnIndex < 0) {
        return HandleNone;
    }

    const QRect rect = columnLayout(columnIndex).imageRect;
    if (!rect.adjusted(-kHandleRadius, -kHandleRadius, kHandleRadius, kHandleRadius).contains(pos)) {
        return HandleNone;
    }

    const bool nearLeft = qAbs(pos.x() - rect.left()) <= kHandleRadius;
    const bool nearRight = qAbs(pos.x() - rect.right()) <= kHandleRadius;
    const bool nearTop = qAbs(pos.y() - rect.top()) <= kHandleRadius;
    const bool nearBottom = qAbs(pos.y() - rect.bottom()) <= kHandleRadius;

    int handle = HandleNone;
    if (nearLeft) {
        handle |= HandleLeft;
    } else if (nearRight) {
        handle |= HandleRight;
    }
    if (nearTop) {
        handle |= HandleTop;
    } else if (nearBottom) {
        handle |= HandleBottom;
    }
    return handle;
}

int MultiCompareWidget::columnIndexAt(const QPoint& pos) const {
    for (int i = 0; i < m_items.size(); ++i) {
        const QRect availableRect = columnLayout(i).availableRect;
        if (availableRect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void MultiCompareWidget::clampFocus() {
    qreal minFocusX = 0.5;
    qreal maxFocusX = 0.5;
    qreal minFocusY = 0.5;
    qreal maxFocusY = 0.5;
    bool initialized = false;

    for (int i = 0; i < m_items.size(); ++i) {
        const QImage& image = m_items[i].image.image;
        const ColumnLayout layout = columnLayout(i);
        if (image.isNull() || layout.availableRect.width() <= 0 || layout.availableRect.height() <= 0) {
            continue;
        }

        const QSize base = image.size().scaled(layout.availableRect.size(), Qt::KeepAspectRatio);
        const qreal scaledW = base.width() * m_zoom;
        const qreal scaledH = base.height() * m_zoom;
        const qreal visibleW = static_cast<qreal>(layout.imageRect.width());
        const qreal visibleH = static_cast<qreal>(layout.imageRect.height());
        const qreal halfVisibleX = scaledW <= visibleW ? 0.5 : (visibleW / (2.0 * scaledW));
        const qreal halfVisibleY = scaledH <= visibleH ? 0.5 : (visibleH / (2.0 * scaledH));

        if (!initialized) {
            minFocusX = halfVisibleX;
            maxFocusX = 1.0 - halfVisibleX;
            minFocusY = halfVisibleY;
            maxFocusY = 1.0 - halfVisibleY;
            initialized = true;
        } else {
            minFocusX = qMax(minFocusX, halfVisibleX);
            maxFocusX = qMin(maxFocusX, 1.0 - halfVisibleX);
            minFocusY = qMax(minFocusY, halfVisibleY);
            maxFocusY = qMin(maxFocusY, 1.0 - halfVisibleY);
        }
    }

    if (!initialized || minFocusX > maxFocusX) {
        m_focus.setX(0.5);
    } else {
        m_focus.setX(qBound(minFocusX, m_focus.x(), maxFocusX));
    }
    if (!initialized || minFocusY > maxFocusY) {
        m_focus.setY(0.5);
    } else {
        m_focus.setY(qBound(minFocusY, m_focus.y(), maxFocusY));
    }
}

void MultiCompareWidget::clampViewportRect() {
    m_viewportRectNormalized.setLeft(qBound(0.0, m_viewportRectNormalized.left(), 1.0));
    m_viewportRectNormalized.setTop(qBound(0.0, m_viewportRectNormalized.top(), 1.0));
    m_viewportRectNormalized.setRight(qBound(0.0, m_viewportRectNormalized.right(), 1.0));
    m_viewportRectNormalized.setBottom(qBound(0.0, m_viewportRectNormalized.bottom(), 1.0));

    if (m_viewportRectNormalized.width() < kMinViewportSize) {
        if (m_viewportHandle & HandleLeft) {
            m_viewportRectNormalized.setLeft(m_viewportRectNormalized.right() - kMinViewportSize);
        } else {
            m_viewportRectNormalized.setRight(m_viewportRectNormalized.left() + kMinViewportSize);
        }
    }
    if (m_viewportRectNormalized.height() < kMinViewportSize) {
        if (m_viewportHandle & HandleTop) {
            m_viewportRectNormalized.setTop(m_viewportRectNormalized.bottom() - kMinViewportSize);
        } else {
            m_viewportRectNormalized.setBottom(m_viewportRectNormalized.top() + kMinViewportSize);
        }
    }

    if (m_viewportRectNormalized.left() < 0.0) {
        m_viewportRectNormalized.moveLeft(0.0);
    }
    if (m_viewportRectNormalized.top() < 0.0) {
        m_viewportRectNormalized.moveTop(0.0);
    }
    if (m_viewportRectNormalized.right() > 1.0) {
        m_viewportRectNormalized.moveRight(1.0);
    }
    if (m_viewportRectNormalized.bottom() > 1.0) {
        m_viewportRectNormalized.moveBottom(1.0);
    }
}

void MultiCompareWidget::resetView() {
    m_zoom = 1.0;
    m_focus = QPointF(0.5, 0.5);
    m_viewportRectNormalized = QRectF(0.0, 0.0, 1.0, 1.0);
    clampViewportRect();
    clampFocus();
    update();
}

QRect MultiCompareWidget::exportContentRect(const QRect& targetRect) const {
    if (m_items.isEmpty()) {
        return targetRect;
    }

    QRect bounds;
    bool initialized = false;
    const LayoutMetrics metrics = layoutMetrics();

    for (int i = 0; i < m_items.size(); ++i) {
        const ColumnLayout layout = columnLayout(i);
        if (!layout.outerRect.isValid()) {
            continue;
        }

        const QRect labelRect = titleRect(layout);
        const QRect itemRect = labelRect.united(layout.imageRect).adjusted(-2, -2, 2, 2);
        if (!initialized) {
            bounds = itemRect;
            initialized = true;
        } else {
            bounds = bounds.united(itemRect);
        }
    }

    if (!initialized) {
        return targetRect;
    }

    return bounds.intersected(targetRect);
}

void MultiCompareWidget::drawScene(QPainter& painter, const QRect& targetRect) const {
    painter.fillRect(targetRect, QColor(20, 20, 20));

    if (m_items.isEmpty()) {
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(targetRect, Qt::AlignCenter, "Load multiple images to compare side by side");
        return;
    }

    for (int i = 0; i < m_items.size(); ++i) {
        const ColumnLayout layout = columnLayout(i);
        if (!layout.outerRect.isValid()) {
            continue;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(32, 32, 32));
        painter.drawRoundedRect(layout.outerRect, 10, 10);

        const QRect labelRect = titleRect(layout);
        QFont titleFont = painter.font();
        titleFont.setPointSize(titleFont.pointSize() + 2);
        painter.setFont(titleFont);
        painter.setPen(QColor(245, 245, 245));
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_items[i].label, Qt::ElideMiddle, labelRect.width()));

        if (m_hoveredColumn == i || m_hoveredRemoveIndex == i) {
            const QRect removeRect = removeButtonRect(layout);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_hoveredRemoveIndex == i ? QColor(220, 90, 90, 230) : QColor(255, 255, 255, 40));
            painter.drawRoundedRect(removeRect, 5, 5);
            painter.setPen(QPen(m_hoveredRemoveIndex == i ? QColor(255, 255, 255) : QColor(245, 245, 245, 220), 1.5));
            painter.drawLine(removeRect.left() + 5, removeRect.top() + 5, removeRect.right() - 5, removeRect.bottom() - 5);
            painter.drawLine(removeRect.right() - 5, removeRect.top() + 5, removeRect.left() + 5, removeRect.bottom() - 5);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(12, 12, 12));
        painter.drawRoundedRect(layout.imageRect.adjusted(-1, -1, 1, 1), 8, 8);

        painter.setClipRect(layout.imageRect);
        const QRectF drawRect = imageDrawRect(m_items[i].image, layout.availableRect);
        if (!m_items[i].image.image.isNull()) {
            painter.drawImage(drawRect, m_items[i].image.image);
        } else {
            painter.fillRect(layout.imageRect, QColor(60, 60, 60));
        }
        painter.setClipping(false);

        painter.setPen(QPen(QColor(255, 255, 255, 60), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(layout.imageRect, 6, 6);
    }

}

void MultiCompareWidget::drawViewportOverlay(QPainter& painter) const {
    if (m_items.isEmpty()) {
        return;
    }
    if (m_dragMode == DragMode::None && m_hoveredColumn < 0) {
        return;
    }

    const int start = (m_dragMode == DragMode::ResizeViewport || m_dragMode == DragMode::Pan)
                          ? 0
                          : m_hoveredColumn;
    const int end = (m_dragMode == DragMode::ResizeViewport || m_dragMode == DragMode::Pan)
                        ? m_items.size() - 1
                        : m_hoveredColumn;

    painter.setPen(QPen(QColor(255, 210, 90, 180), 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (int i = start; i <= end; ++i) {
        if (i < 0 || i >= m_items.size()) {
            continue;
        }
        const QRect rect = columnLayout(i).imageRect;
        if (!rect.isValid()) {
            continue;
        }
        painter.drawRoundedRect(rect, 6, 6);
    }
}

void MultiCompareWidget::updateCursorForPosition(const QPoint& pos) {
    if (m_dragMode == DragMode::Pan) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_hoveredRemoveIndex >= 0) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    if (m_dragMode == DragMode::ResizeViewport) {
        const int handle = m_viewportHandle;
        if ((handle & HandleLeft) && (handle & HandleTop)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if ((handle & HandleRight) && (handle & HandleBottom)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if ((handle & HandleRight) && (handle & HandleTop)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if ((handle & HandleLeft) && (handle & HandleBottom)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if ((handle & HandleLeft) || (handle & HandleRight)) {
            setCursor(Qt::SizeHorCursor);
        } else if ((handle & HandleTop) || (handle & HandleBottom)) {
            setCursor(Qt::SizeVerCursor);
        }
        return;
    }

    const int handle = viewportHandleAt(pos);
    if ((handle & HandleLeft) && (handle & HandleTop)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if ((handle & HandleRight) && (handle & HandleBottom)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if ((handle & HandleRight) && (handle & HandleTop)) {
        setCursor(Qt::SizeBDiagCursor);
    } else if ((handle & HandleLeft) && (handle & HandleBottom)) {
        setCursor(Qt::SizeBDiagCursor);
    } else if ((handle & HandleLeft) || (handle & HandleRight)) {
        setCursor(Qt::SizeHorCursor);
    } else if ((handle & HandleTop) || (handle & HandleBottom)) {
        setCursor(Qt::SizeVerCursor);
    } else if (columnIndexAt(pos) >= 0) {
        setCursor(Qt::OpenHandCursor);
    } else {
        unsetCursor();
    }
}

void MultiCompareWidget::beginInlineTitleEdit(int index) {
    if (!m_titleEditor || index < 0 || index >= m_items.size()) {
        return;
    }
    m_editingTitleIndex = index;
    const QRect rect = titleRect(columnLayout(index));
    m_titleEditor->setGeometry(rect);
    m_titleEditor->setText(m_items[index].label);
    m_titleEditor->show();
    m_titleEditor->raise();
    m_titleEditor->setFocus();
    m_titleEditor->selectAll();
}

void MultiCompareWidget::commitInlineTitleEdit() {
    if (!m_titleEditor || m_editingTitleIndex < 0 || m_editingTitleIndex >= m_items.size()) {
        return;
    }
    const QString next = m_titleEditor->text().trimmed();
    if (!next.isEmpty()) {
        m_items[m_editingTitleIndex].label = next;
        emit itemLabelEdited(m_editingTitleIndex, next);
    }
    m_titleEditor->hide();
    m_editingTitleIndex = -1;
    update();
}

void MultiCompareWidget::cancelInlineTitleEdit() {
    if (!m_titleEditor) {
        return;
    }
    m_titleEditor->hide();
    m_editingTitleIndex = -1;
}
