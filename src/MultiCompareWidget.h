#pragma once

#include "ImageLoader.h"

#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class QEvent;
class QPainter;
class QLineEdit;

class MultiCompareWidget : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString label;
        LoadedImage image;
    };

    explicit MultiCompareWidget(QWidget* parent = nullptr);

    void setItems(const QVector<Item>& items);
    void setItemLabel(int index, const QString& label);
    void clearItems();
    bool hasItems() const;
    QImage renderComparisonImage(qreal devicePixelRatio = 1.0) const;

signals:
    void itemLabelEdited(int index, const QString& label);
    void itemRemoveRequested(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class DragMode {
        None,
        Pan,
        ResizeViewport
    };

    enum ViewportHandle {
        HandleNone = 0,
        HandleLeft = 1 << 0,
        HandleRight = 1 << 1,
        HandleTop = 1 << 2,
        HandleBottom = 1 << 3
    };

    struct LayoutMetrics {
        QRect contentRect;
        int columns = 0;
        int spacing = 0;
        int labelHeight = 0;
    };

    struct ColumnLayout {
        QRect outerRect;
        QRect availableRect;
        QRect imageRect;
        int index = -1;
    };

    LayoutMetrics layoutMetrics() const;
    ColumnLayout columnLayout(int index) const;
    QRect viewportRectFromAvailable(const QRect& availableRect) const;
    QRectF imageDrawRect(const LoadedImage& image, const QRect& availableRect) const;
    QRect titleRect(const ColumnLayout& layout) const;
    QRect removeButtonRect(const ColumnLayout& layout) const;
    QPointF widgetPosToNormalized(const QPointF& pos, int columnIndex, bool* ok = nullptr) const;
    int viewportHandleAt(const QPoint& pos) const;
    int columnIndexAt(const QPoint& pos) const;
    void clampFocus();
    void clampViewportRect();
    void resetView();
    QRect exportContentRect(const QRect& targetRect) const;
    void drawScene(QPainter& painter, const QRect& targetRect) const;
    void drawViewportOverlay(QPainter& painter) const;
    void updateCursorForPosition(const QPoint& pos);
    void beginInlineTitleEdit(int index);
    void commitInlineTitleEdit();
    void cancelInlineTitleEdit();

    QVector<Item> m_items;
    DragMode m_dragMode = DragMode::None;
    int m_viewportHandle = HandleNone;
    int m_activeColumn = -1;
    int m_hoveredColumn = -1;
    int m_hoveredRemoveIndex = -1;
    QPoint m_lastMousePos;
    QPointF m_dragAnchorFocus{0.5, 0.5};
    QRectF m_dragAnchorViewportRect;
    double m_zoom = 1.0;
    QPointF m_focus{0.5, 0.5};
    QRectF m_viewportRectNormalized{0.0, 0.0, 1.0, 1.0};
    QLineEdit* m_titleEditor = nullptr;
    int m_editingTitleIndex = -1;
};
