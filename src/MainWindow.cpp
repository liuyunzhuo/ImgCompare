#include "MainWindow.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QShortcut>
#include <QUrl>
#include <QWidget>

namespace {
QComboBox* createFormatCombo(QWidget* parent) {
    auto* combo = new QComboBox(parent);
    combo->addItem("Auto", static_cast<int>(PixelFormat::Auto));
    combo->addItem("PNG/JPG", static_cast<int>(PixelFormat::PngJpg));
    combo->addItem("YUV420P", static_cast<int>(PixelFormat::YUV420P));
    combo->addItem("YUV444P", static_cast<int>(PixelFormat::YUV444P));
    combo->addItem("NV12", static_cast<int>(PixelFormat::NV12));
    combo->addItem("NV16", static_cast<int>(PixelFormat::NV16));
    return combo;
}

QSpinBox* createSizeSpin(QWidget* parent) {
    auto* spin = new QSpinBox(parent);
    spin->setRange(1, 8192);
    spin->setValue(1920);
    return spin;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("ImgCompare");
    resize(1200, 720);
    setAcceptDrops(true);

    auto* central = new QWidget(this);
    m_rootLayout = new QVBoxLayout(central);
    m_rootLayout->setContentsMargins(8, 8, 8, 8);
    m_rootLayout->setSpacing(8);

    m_controlPanel = new QWidget(central);
    auto* toolRow = new QHBoxLayout(m_controlPanel);
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(12);

    auto* leftGroup = new QWidget(m_controlPanel);
    auto* leftForm = new QFormLayout(leftGroup);
    auto* leftLoadBtn = new QPushButton("Load Left", leftGroup);
    m_leftFormat = createFormatCombo(leftGroup);
    m_leftW = createSizeSpin(leftGroup);
    m_leftH = createSizeSpin(leftGroup);
    leftForm->addRow(leftLoadBtn);
    leftForm->addRow("Format", m_leftFormat);
    leftForm->addRow("Width", m_leftW);
    leftForm->addRow("Height", m_leftH);

    auto* rightGroup = new QWidget(m_controlPanel);
    auto* rightForm = new QFormLayout(rightGroup);
    auto* rightLoadBtn = new QPushButton("Load Right", rightGroup);
    m_rightFormat = createFormatCombo(rightGroup);
    m_rightW = createSizeSpin(rightGroup);
    m_rightH = createSizeSpin(rightGroup);
    rightForm->addRow(rightLoadBtn);
    rightForm->addRow("Format", m_rightFormat);
    rightForm->addRow("Width", m_rightW);
    rightForm->addRow("Height", m_rightH);

    auto* fullscreenBtn = new QPushButton("Fullscreen (F11)", m_controlPanel);
    fullscreenBtn->setMinimumHeight(36);

    toolRow->addWidget(leftGroup);
    toolRow->addWidget(rightGroup);
    toolRow->addStretch();
    toolRow->addWidget(fullscreenBtn);

    m_compareWidget = new CompareWidget(central);

    m_rootLayout->addWidget(m_controlPanel);
    m_rootLayout->addWidget(m_compareWidget, 1);
    setCentralWidget(central);

    connect(leftLoadBtn, &QPushButton::clicked, this, &MainWindow::loadLeftImage);
    connect(rightLoadBtn, &QPushButton::clicked, this, &MainWindow::loadRightImage);
    connect(fullscreenBtn, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);

    auto* f11 = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(f11, &QShortcut::activated, this, &MainWindow::toggleFullscreen);

    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, [this]() {
        if (isFullScreen()) {
            applyFullscreenUi(false);
            showNormal();
        }
    });

    const auto refreshSizeInputState = [this]() {
        const auto leftFmt = comboToFormat(m_leftFormat);
        const bool leftYuv = leftFmt == PixelFormat::YUV420P
                          || leftFmt == PixelFormat::YUV444P
                          || leftFmt == PixelFormat::NV12
                          || leftFmt == PixelFormat::NV16;
        m_leftW->setEnabled(leftYuv);
        m_leftH->setEnabled(leftYuv);

        const auto rightFmt = comboToFormat(m_rightFormat);
        const bool rightYuv = rightFmt == PixelFormat::YUV420P
                           || rightFmt == PixelFormat::YUV444P
                           || rightFmt == PixelFormat::NV12
                           || rightFmt == PixelFormat::NV16;
        m_rightW->setEnabled(rightYuv);
        m_rightH->setEnabled(rightYuv);
    };

    connect(m_leftFormat, &QComboBox::currentIndexChanged, this, refreshSizeInputState);
    connect(m_rightFormat, &QComboBox::currentIndexChanged, this, refreshSizeInputState);
    refreshSizeInputState();
}

PixelFormat MainWindow::comboToFormat(const QComboBox* combo) {
    return static_cast<PixelFormat>(combo->currentData().toInt());
}

ImageSource MainWindow::collectSource(bool left) const {
    ImageSource src;
    if (left) {
        src.path = m_leftPath;
        src.format = comboToFormat(m_leftFormat);
        src.width = m_leftW->value();
        src.height = m_leftH->value();
    } else {
        src.path = m_rightPath;
        src.format = comboToFormat(m_rightFormat);
        src.width = m_rightW->value();
        src.height = m_rightH->value();
    }
    return src;
}

void MainWindow::loadLeftImage() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Select Left Image",
        QString(),
        "Images (*.png *.jpg *.jpeg *.yuv *.nv12 *.nv16 *.i420);;All Files (*.*)");
    if (path.isEmpty()) {
        return;
    }
    loadImageFromPath(path, true);
}

void MainWindow::loadRightImage() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Select Right Image",
        QString(),
        "Images (*.png *.jpg *.jpeg *.yuv *.nv12 *.nv16 *.i420);;All Files (*.*)");
    if (path.isEmpty()) {
        return;
    }
    loadImageFromPath(path, false);
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) {
        applyFullscreenUi(false);
        showNormal();
    } else {
        applyFullscreenUi(true);
        showFullScreen();
    }
}

void MainWindow::applyFullscreenUi(bool fullscreen) {
    if (m_controlPanel) {
        m_controlPanel->setVisible(!fullscreen);
    }
    if (m_rootLayout) {
        if (fullscreen) {
            m_rootLayout->setContentsMargins(0, 0, 0, 0);
            m_rootLayout->setSpacing(0);
        } else {
            m_rootLayout->setContentsMargins(8, 8, 8, 8);
            m_rootLayout->setSpacing(8);
        }
    }
}

bool MainWindow::loadImageFromPath(const QString& path, bool left, bool showError) {
    if (path.isEmpty()) {
        return false;
    }

    if (left) {
        m_leftPath = path;
    } else {
        m_rightPath = path;
    }

    LoadedImage img;
    QString err;
    if (!ImageLoader::load(collectSource(left), img, err)) {
        if (showError) {
            QMessageBox::warning(this, left ? "Load Left Failed" : "Load Right Failed", err);
        }
        return false;
    }

    if (left) {
        m_compareWidget->setLeftImage(img);
    } else {
        m_compareWidget->setRightImage(img);
    }
    return true;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo fi(url.toLocalFile());
        if (fi.exists() && fi.isFile()) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    QStringList files;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo fi(url.toLocalFile());
        if (fi.exists() && fi.isFile()) {
            files.push_back(fi.absoluteFilePath());
        }
    }

    if (files.isEmpty()) {
        event->ignore();
        return;
    }

    if (files.size() >= 2) {
        loadImageFromPath(files.at(0), true);
        loadImageFromPath(files.at(1), false);
    } else {
        const bool toLeft = event->position().x() < (width() / 2.0);
        loadImageFromPath(files.first(), toLeft);
    }

    event->acceptProposedAction();
}
