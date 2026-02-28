#pragma once

#include "CompareWidget.h"
#include "ImageLoader.h"

#include <QComboBox>
#include <QDragEnterEvent>
#include <QMainWindow>
#include <QDropEvent>
#include <QSpinBox>
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
    void toggleFullscreen();

private:
    ImageSource collectSource(bool left) const;
    static PixelFormat comboToFormat(const QComboBox* combo);
    void applyFullscreenUi(bool fullscreen);
    bool loadImageFromPath(const QString& path, bool left, bool showError = true);

    CompareWidget* m_compareWidget = nullptr;
    QVBoxLayout* m_rootLayout = nullptr;

    QComboBox* m_leftFormat = nullptr;
    QComboBox* m_rightFormat = nullptr;
    QSpinBox* m_leftW = nullptr;
    QSpinBox* m_leftH = nullptr;
    QSpinBox* m_rightW = nullptr;
    QSpinBox* m_rightH = nullptr;
    QWidget* m_controlPanel = nullptr;
    QAction* m_showPixelInfoAction = nullptr;
    QAction* m_showPixelDiffAction = nullptr;
    QAction* m_showPsnrAction = nullptr;
    QString m_leftPath;
    QString m_rightPath;
};
