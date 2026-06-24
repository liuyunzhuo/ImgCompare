#pragma once

#include "CompareWidget.h"
#include "ImageLoader.h"
#include "MultiCompareWidget.h"

#include <QComboBox>
#include <QDragEnterEvent>
#include <QMainWindow>
#include <QDropEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void loadLeftImage();
    void loadRightImage();
    void loadMultiImages();
    void saveLeftImage();
    void saveRightImage();
    void clearMultiImages();
    void copyMultiComparisonToClipboard();
    void toggleFullscreen();

private:
    ImageSource collectSource(bool left) const;
    static PixelFormat comboToFormat(const QComboBox* combo);
    void applyFullscreenUi(bool fullscreen);
    bool loadImageFromPath(const QString& path, bool left, bool showError = true);
    bool saveImageToPath(const QString& path, bool left, bool showError = true);
    bool loadMultiItemFromPath(const QString& path, MultiCompareWidget::Item& outItem, QString& err) const;
    QVector<MultiCompareWidget::Item> loadMultiItemsFromPaths(const QStringList& paths, QStringList& failed) const;

    CompareWidget* m_compareWidget = nullptr;
    MultiCompareWidget* m_multiCompareWidget = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    QVBoxLayout* m_rootLayout = nullptr;

    QComboBox* m_modeCombo = nullptr;
    QComboBox* m_leftFormat = nullptr;
    QComboBox* m_rightFormat = nullptr;
    QComboBox* m_multiFormat = nullptr;
    QSpinBox* m_leftW = nullptr;
    QSpinBox* m_leftH = nullptr;
    QSpinBox* m_rightW = nullptr;
    QSpinBox* m_rightH = nullptr;
    QSpinBox* m_multiW = nullptr;
    QSpinBox* m_multiH = nullptr;
    QWidget* m_controlPanel = nullptr;
    QAction* m_showPixelInfoAction = nullptr;
    QAction* m_showPixelDiffAction = nullptr;
    QAction* m_showPsnrAction = nullptr;
    QAction* m_showYuvValuesAction = nullptr;
    QAction* m_showRgbValuesAction = nullptr;
    LoadedImage m_leftImageData;
    LoadedImage m_rightImageData;
    QVector<MultiCompareWidget::Item> m_multiItems;
    QString m_leftPath;
    QString m_rightPath;
};
