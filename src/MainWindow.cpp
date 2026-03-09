#include "MainWindow.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QMessageBox>
#include <QMimeData>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
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

int chromaWidth(const YuvPlanes& yuv) {
    return yuv.subsampling == ChromaSubsampling::Cs444 ? yuv.width : (yuv.width / 2);
}

int chromaHeight(const YuvPlanes& yuv) {
    return yuv.subsampling == ChromaSubsampling::Cs420 ? (yuv.height / 2) : yuv.height;
}

bool hasValidYuv(const LoadedImage& image) {
    if (image.yuv.width <= 0 || image.yuv.height <= 0) {
        return false;
    }
    const int ySize = image.yuv.width * image.yuv.height;
    const int cSize = chromaWidth(image.yuv) * chromaHeight(image.yuv);
    return image.yuv.y.size() >= ySize && image.yuv.u.size() >= cSize && image.yuv.v.size() >= cSize;
}

int samplePlaneMapped(const QByteArray& plane, int planeW, int planeH, int frameW, int frameH, int x, int y) {
    if (planeW <= 0 || planeH <= 0 || frameW <= 0 || frameH <= 0 || plane.isEmpty()) {
        return 0;
    }
    const int sx = qBound(0, (x * planeW) / frameW, planeW - 1);
    const int sy = qBound(0, (y * planeH) / frameH, planeH - 1);
    return static_cast<int>(static_cast<unsigned char>(plane[sy * planeW + sx]));
}

int sampleY(const LoadedImage& image, int x, int y) {
    const int idx = y * image.yuv.width + x;
    return static_cast<int>(static_cast<unsigned char>(image.yuv.y[idx]));
}

int sampleU(const LoadedImage& image, int x, int y) {
    return samplePlaneMapped(image.yuv.u, chromaWidth(image.yuv), chromaHeight(image.yuv), image.yuv.width, image.yuv.height, x, y);
}

int sampleV(const LoadedImage& image, int x, int y) {
    return samplePlaneMapped(image.yuv.v, chromaWidth(image.yuv), chromaHeight(image.yuv), image.yuv.width, image.yuv.height, x, y);
}

bool writeAllBytes(const QString& path, const QByteArray& data, QString& err) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        err = QString("Failed to open output file: %1").arg(path);
        return false;
    }
    if (f.write(data) != data.size()) {
        err = QString("Failed to write output file: %1").arg(path);
        return false;
    }
    return true;
}

bool saveAsYuv444(const LoadedImage& image, const QString& path, QString& err) {
    const int w = image.yuv.width;
    const int h = image.yuv.height;
    QByteArray y;
    QByteArray u;
    QByteArray v;
    y.resize(w * h);
    u.resize(w * h);
    v.resize(w * h);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const int idx = j * w + i;
            y[idx] = static_cast<char>(sampleY(image, i, j));
            u[idx] = static_cast<char>(sampleU(image, i, j));
            v[idx] = static_cast<char>(sampleV(image, i, j));
        }
    }
    return writeAllBytes(path, y + u + v, err);
}

bool saveAsYuv420(const LoadedImage& image, const QString& path, QString& err) {
    const int w = image.yuv.width;
    const int h = image.yuv.height;
    if ((w % 2) != 0 || (h % 2) != 0) {
        err = "YUV420/NV12 output requires even width and height.";
        return false;
    }
    QByteArray y;
    QByteArray u;
    QByteArray v;
    y.resize(w * h);
    u.resize((w / 2) * (h / 2));
    v.resize((w / 2) * (h / 2));
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            y[j * w + i] = static_cast<char>(sampleY(image, i, j));
        }
    }
    for (int cy = 0; cy < h / 2; ++cy) {
        for (int cx = 0; cx < w / 2; ++cx) {
            const int x0 = cx * 2;
            const int y0 = cy * 2;
            const int uAvg = (sampleU(image, x0, y0) + sampleU(image, x0 + 1, y0)
                            + sampleU(image, x0, y0 + 1) + sampleU(image, x0 + 1, y0 + 1)) / 4;
            const int vAvg = (sampleV(image, x0, y0) + sampleV(image, x0 + 1, y0)
                            + sampleV(image, x0, y0 + 1) + sampleV(image, x0 + 1, y0 + 1)) / 4;
            const int idx = cy * (w / 2) + cx;
            u[idx] = static_cast<char>(uAvg);
            v[idx] = static_cast<char>(vAvg);
        }
    }
    return writeAllBytes(path, y + u + v, err);
}

bool saveAsNv12(const LoadedImage& image, const QString& path, QString& err) {
    const int w = image.yuv.width;
    const int h = image.yuv.height;
    if ((w % 2) != 0 || (h % 2) != 0) {
        err = "YUV420/NV12 output requires even width and height.";
        return false;
    }
    QByteArray y;
    QByteArray uv;
    y.resize(w * h);
    uv.resize(w * (h / 2));
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            y[j * w + i] = static_cast<char>(sampleY(image, i, j));
        }
    }
    for (int cy = 0; cy < h / 2; ++cy) {
        for (int cx = 0; cx < w / 2; ++cx) {
            const int x0 = cx * 2;
            const int y0 = cy * 2;
            const int uAvg = (sampleU(image, x0, y0) + sampleU(image, x0 + 1, y0)
                            + sampleU(image, x0, y0 + 1) + sampleU(image, x0 + 1, y0 + 1)) / 4;
            const int vAvg = (sampleV(image, x0, y0) + sampleV(image, x0 + 1, y0)
                            + sampleV(image, x0, y0 + 1) + sampleV(image, x0 + 1, y0 + 1)) / 4;
            const int idx = cy * w + cx * 2;
            uv[idx] = static_cast<char>(uAvg);
            uv[idx + 1] = static_cast<char>(vAvg);
        }
    }
    return writeAllBytes(path, y + uv, err);
}

bool saveAsNv16(const LoadedImage& image, const QString& path, QString& err) {
    const int w = image.yuv.width;
    const int h = image.yuv.height;
    if ((w % 2) != 0) {
        err = "NV16 output requires even width.";
        return false;
    }
    QByteArray y;
    QByteArray uv;
    y.resize(w * h);
    uv.resize(w * h);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            y[j * w + i] = static_cast<char>(sampleY(image, i, j));
        }
    }
    for (int j = 0; j < h; ++j) {
        for (int cx = 0; cx < w / 2; ++cx) {
            const int x0 = cx * 2;
            const int uAvg = (sampleU(image, x0, j) + sampleU(image, x0 + 1, j)) / 2;
            const int vAvg = (sampleV(image, x0, j) + sampleV(image, x0 + 1, j)) / 2;
            const int idx = j * w + cx * 2;
            uv[idx] = static_cast<char>(uAvg);
            uv[idx + 1] = static_cast<char>(vAvg);
        }
    }
    return writeAllBytes(path, y + uv, err);
}

enum class SaveYuvFormat {
    Unknown,
    Yuv444,
    Yuv420,
    Nv12,
    Nv16
};

SaveYuvFormat detectSaveYuvFormatFromName(const QString& path) {
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    const QString base = fi.completeBaseName().toLower();
    const QString fileName = fi.fileName().toLower();
    const auto nameHas = [&](const QString& token) {
        return base.contains(token) || fileName.contains(token);
    };

    if (ext == "yuv444" || nameHas("_i444p") || nameHas("_444p") || nameHas("_i444")) {
        return SaveYuvFormat::Yuv444;
    }
    if (ext == "yuv420" || ext == "i420" || nameHas("_i420p") || nameHas("_420p") || nameHas("_i420")) {
        return SaveYuvFormat::Yuv420;
    }
    if (ext == "nv12" || nameHas("_nv12")) {
        return SaveYuvFormat::Nv12;
    }
    if (ext == "nv16" || nameHas("_nv16")) {
        return SaveYuvFormat::Nv16;
    }
    return SaveYuvFormat::Unknown;
}

bool parseResolutionFromFileName(const QString& path, int& outW, int& outH) {
    const QString name = QFileInfo(path).fileName();
    const QRegularExpression re("(\\d{2,5})\\s*[xX]\\s*(\\d{2,5})");
    const QRegularExpressionMatch m = re.match(name);
    if (!m.hasMatch()) {
        return false;
    }
    bool okW = false;
    bool okH = false;
    const int w = m.captured(1).toInt(&okW);
    const int h = m.captured(2).toInt(&okH);
    if (!okW || !okH || w <= 0 || h <= 0) {
        return false;
    }
    outW = w;
    outH = h;
    return true;
}

QString ensureSaveExtension(const QString& rawPath, const QString& selectedFilter) {
    if (rawPath.isEmpty()) {
        return rawPath;
    }
    const QFileInfo fi(rawPath);
    const QString filter = selectedFilter.toLower();
    const QString suffix = fi.suffix().toLower();
    const QString noExtPath = suffix.isEmpty() ? rawPath : rawPath.left(rawPath.size() - suffix.size() - 1);
    const QString baseNameNoExt = QFileInfo(noExtPath).fileName();
    const QString lowerBase = baseNameNoExt.toLower();
    const bool hasYuvLikeSuffix = (suffix == "yuv" || suffix == "yuv420" || suffix == "i420" || suffix == "yuv444" || suffix == "nv12" || suffix == "nv16");

    if (filter.contains("png")) {
        if (!suffix.isEmpty()) {
            return rawPath;
        }
        return rawPath + ".png";
    }
    if (filter.contains("jpeg")) {
        if (!suffix.isEmpty()) {
            return rawPath;
        }
        return rawPath + ".jpg";
    }
    if (filter.contains("nv12")) {
        if (!suffix.isEmpty() && !hasYuvLikeSuffix) {
            return rawPath;
        }
        if (lowerBase.contains("_nv12")) {
            return noExtPath + ".yuv";
        }
        return noExtPath + "_nv12.yuv";
    }
    if (filter.contains("nv16")) {
        if (!suffix.isEmpty() && !hasYuvLikeSuffix) {
            return rawPath;
        }
        if (lowerBase.contains("_nv16")) {
            return noExtPath + ".yuv";
        }
        return noExtPath + "_nv16.yuv";
    }
    if (filter.contains("yuv420") || filter.contains("i420")) {
        if (!suffix.isEmpty() && !hasYuvLikeSuffix) {
            return rawPath;
        }
        if (lowerBase.contains("_420p") || lowerBase.contains("_i420p") || lowerBase.contains("_i420")) {
            return noExtPath + ".yuv";
        }
        return noExtPath + "_420p.yuv";
    }
    if (filter.contains("yuv444") || filter.contains("i444")) {
        if (!suffix.isEmpty() && !hasYuvLikeSuffix) {
            return rawPath;
        }
        if (lowerBase.contains("_444p") || lowerBase.contains("_i444p") || lowerBase.contains("_i444")) {
            return noExtPath + ".yuv";
        }
        return noExtPath + "_I444p.yuv";
    }

    if (suffix.isEmpty() && (detectSaveYuvFormatFromName(rawPath) != SaveYuvFormat::Unknown || filter.contains("yuv"))) {
        return rawPath + ".yuv";
    }
    return rawPath;
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
    auto* leftSaveBtn = new QPushButton("Save Left", leftGroup);
    m_leftFormat = createFormatCombo(leftGroup);
    m_leftW = createSizeSpin(leftGroup);
    m_leftH = createSizeSpin(leftGroup);
    leftForm->addRow(leftLoadBtn);
    leftForm->addRow(leftSaveBtn);
    leftForm->addRow("Format", m_leftFormat);
    leftForm->addRow("Width", m_leftW);
    leftForm->addRow("Height", m_leftH);

    auto* rightGroup = new QWidget(m_controlPanel);
    auto* rightForm = new QFormLayout(rightGroup);
    auto* rightLoadBtn = new QPushButton("Load Right", rightGroup);
    auto* rightSaveBtn = new QPushButton("Save Right", rightGroup);
    m_rightFormat = createFormatCombo(rightGroup);
    m_rightW = createSizeSpin(rightGroup);
    m_rightH = createSizeSpin(rightGroup);
    rightForm->addRow(rightLoadBtn);
    rightForm->addRow(rightSaveBtn);
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

    auto* viewMenu = menuBar()->addMenu("View");
    m_showPixelInfoAction = viewMenu->addAction("Show Pixel Info");
    m_showPixelInfoAction->setCheckable(true);
    m_showPixelInfoAction->setChecked(true);
    m_showPixelDiffAction = viewMenu->addAction("Show Pixel Diff");
    m_showPixelDiffAction->setCheckable(true);
    m_showPixelDiffAction->setChecked(true);
    m_showPsnrAction = viewMenu->addAction("Show PSNR");
    m_showPsnrAction->setCheckable(true);
    m_showPsnrAction->setChecked(true);

    connect(m_showPixelInfoAction, &QAction::toggled, m_compareWidget, &CompareWidget::setShowPixelInfo);
    connect(m_showPixelDiffAction, &QAction::toggled, m_compareWidget, &CompareWidget::setShowPixelDiff);
    connect(m_showPsnrAction, &QAction::toggled, m_compareWidget, &CompareWidget::setShowPsnr);

    connect(leftLoadBtn, &QPushButton::clicked, this, &MainWindow::loadLeftImage);
    connect(leftSaveBtn, &QPushButton::clicked, this, &MainWindow::saveLeftImage);
    connect(rightLoadBtn, &QPushButton::clicked, this, &MainWindow::loadRightImage);
    connect(rightSaveBtn, &QPushButton::clicked, this, &MainWindow::saveRightImage);
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

void MainWindow::saveLeftImage() {
    if (m_leftImageData.image.isNull()) {
        QMessageBox::information(this, "Save Left", "Left image is empty.");
        return;
    }
    QString selectedFilter;
    const QString rawPath = QFileDialog::getSaveFileName(
        this,
        "Save Left Image",
        QFileInfo(m_leftPath).completeBaseName(),
        "YUV444 (*.yuv);;YUV420P (*.yuv);;NV12 (*.yuv);;NV16 (*.yuv);;PNG (*.png);;JPEG (*.jpg *.jpeg)",
        &selectedFilter);
    if (rawPath.isEmpty()) {
        return;
    }
    const QString path = ensureSaveExtension(rawPath, selectedFilter);
    saveImageToPath(path, true);
}

void MainWindow::saveRightImage() {
    if (m_rightImageData.image.isNull()) {
        QMessageBox::information(this, "Save Right", "Right image is empty.");
        return;
    }
    QString selectedFilter;
    const QString rawPath = QFileDialog::getSaveFileName(
        this,
        "Save Right Image",
        QFileInfo(m_rightPath).completeBaseName(),
        "YUV444 (*.yuv);;YUV420P (*.yuv);;NV12 (*.yuv);;NV16 (*.yuv);;PNG (*.png);;JPEG (*.jpg *.jpeg)",
        &selectedFilter);
    if (rawPath.isEmpty()) {
        return;
    }
    const QString path = ensureSaveExtension(rawPath, selectedFilter);
    saveImageToPath(path, false);
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

    if (left) {
        if (comboToFormat(m_leftFormat) == PixelFormat::Auto) {
            int w = 0;
            int h = 0;
            if (parseResolutionFromFileName(path, w, h)) {
                m_leftW->setValue(w);
                m_leftH->setValue(h);
            }
        }
    } else {
        if (comboToFormat(m_rightFormat) == PixelFormat::Auto) {
            int w = 0;
            int h = 0;
            if (parseResolutionFromFileName(path, w, h)) {
                m_rightW->setValue(w);
                m_rightH->setValue(h);
            }
        }
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
        m_leftImageData = img;
        m_compareWidget->setLeftImage(img);
    } else {
        m_rightImageData = img;
        m_compareWidget->setRightImage(img);
    }
    return true;
}

bool MainWindow::saveImageToPath(const QString& path, bool left, bool showError) {
    const LoadedImage& image = left ? m_leftImageData : m_rightImageData;
    if (image.image.isNull() || !hasValidYuv(image)) {
        if (showError) {
            QMessageBox::warning(this, left ? "Save Left Failed" : "Save Right Failed", "Image data is invalid.");
        }
        return false;
    }

    const QString ext = QFileInfo(path).suffix().toLower();
    QString err;
    bool ok = false;
    if (ext == "png" || ext == "jpg" || ext == "jpeg") {
        ok = image.image.save(path);
        if (!ok) {
            err = "Failed to save image file.";
        }
    } else {
        const SaveYuvFormat fmt = detectSaveYuvFormatFromName(path);
        if (fmt == SaveYuvFormat::Yuv444) {
            ok = saveAsYuv444(image, path, err);
        } else if (fmt == SaveYuvFormat::Yuv420) {
            ok = saveAsYuv420(image, path, err);
        } else if (fmt == SaveYuvFormat::Nv12) {
            ok = saveAsNv12(image, path, err);
        } else if (fmt == SaveYuvFormat::Nv16) {
            ok = saveAsNv16(image, path, err);
        } else {
            err = "YUV filename must include format hint: _420p/_i420p, _444p/_i444p, _nv12, or _nv16.";
        }
    }

    if (!ok && showError) {
        QMessageBox::warning(this, left ? "Save Left Failed" : "Save Right Failed", err);
    }
    return ok;
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


