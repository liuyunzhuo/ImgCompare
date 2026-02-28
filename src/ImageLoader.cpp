#include "ImageLoader.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtMath>

namespace {
int clampToByte(int v) {
    return qBound(0, v, 255);
}

QRgb yuvToRgb(int y, int u, int v) {
    // Inverse of rgbToYuvBt702() in full-range BT.702/709-style transform.
    const double yd = static_cast<double>(y);
    const double ud = static_cast<double>(u) - 128.0;
    const double vd = static_cast<double>(v) - 128.0;

    const int r = qRound(yd + 1.574800 * vd);
    const int g = qRound(yd - 0.187324 * ud - 0.468124 * vd);
    const int b = qRound(yd + 1.855600 * ud);
    return qRgb(clampToByte(r), clampToByte(g), clampToByte(b));
}

void rgbToYuvBt702(int r, int g, int b, int& y, int& u, int& v) {
    const double yd = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double ud = -0.114572 * r - 0.385428 * g + 0.500000 * b + 128.0;
    const double vd = 0.500000 * r - 0.454153 * g - 0.045847 * b + 128.0;
    y = clampToByte(qRound(yd));
    u = clampToByte(qRound(ud));
    v = clampToByte(qRound(vd));
}

PixelFormat detectFormat(const ImageSource& src) {
    if (src.format != PixelFormat::Auto) {
        return src.format;
    }

    const QFileInfo fi(src.path);
    const QString ext = fi.suffix().toLower();
    const QString base = fi.completeBaseName().toLower();
    const QString fileName = fi.fileName().toLower();

    const auto nameHas = [&](const QString& token) {
        return base.contains(token) || fileName.contains(token);
    };

    const auto nameMatch = [&](const QString& pattern) {
        return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(fileName).hasMatch();
    };

    if (nameHas("_nv12") || nameHas(".nv12")) {
        return PixelFormat::NV12;
    }
    if (nameHas("_nv16") || nameHas(".nv16")) {
        return PixelFormat::NV16;
    }
    if (nameHas("_i420p") || nameHas("_420p") || nameHas("_i420") || nameMatch("(^|[_\\-.])420($|[_\\-.])")) {
        return PixelFormat::YUV420P;
    }
    if (nameHas("_i444p") || nameHas("_444p") || nameHas("_i444") || nameMatch("(^|[_\\-.])444($|[_\\-.])")) {
        return PixelFormat::YUV444P;
    }

    if (ext == "png" || ext == "jpg" || ext == "jpeg") {
        return PixelFormat::PngJpg;
    }
    if (ext == "yuv420" || ext == "i420") {
        return PixelFormat::YUV420P;
    }
    if (ext == "yuv444") {
        return PixelFormat::YUV444P;
    }
    if (ext == "yuv") {
        // Generic .yuv without naming hint: default to 420 for compatibility.
        return PixelFormat::YUV420P;
    }
    if (ext == "nv12") {
        return PixelFormat::NV12;
    }
    if (ext == "nv16") {
        return PixelFormat::NV16;
    }

    return PixelFormat::PngJpg;
}

bool buildDisplayImageFromYuv(const YuvPlanes& yuv, QImage& outImage, QString& err) {
    if (yuv.width <= 0 || yuv.height <= 0) {
        err = "Invalid YUV size.";
        return false;
    }

    const int ySize = yuv.width * yuv.height;
    const int cW = yuv.subsampling == ChromaSubsampling::Cs444 ? yuv.width : (yuv.width / 2);
    const int cH = yuv.subsampling == ChromaSubsampling::Cs420 ? (yuv.height / 2) : yuv.height;
    const int cSize = cW * cH;
    if (yuv.y.size() < ySize || yuv.u.size() < cSize || yuv.v.size() < cSize) {
        err = "YUV plane data is incomplete.";
        return false;
    }

    const uchar* yPlane = reinterpret_cast<const uchar*>(yuv.y.constData());
    const uchar* uPlane = reinterpret_cast<const uchar*>(yuv.u.constData());
    const uchar* vPlane = reinterpret_cast<const uchar*>(yuv.v.constData());

    outImage = QImage(yuv.width, yuv.height, QImage::Format_RGB888);
    for (int j = 0; j < yuv.height; ++j) {
        uchar* dst = outImage.scanLine(j);
        for (int i = 0; i < yuv.width; ++i) {
            const int yIdx = j * yuv.width + i;
            const int cx = yuv.subsampling == ChromaSubsampling::Cs444 ? i : (i / 2);
            const int cy = yuv.subsampling == ChromaSubsampling::Cs420 ? (j / 2) : j;
            const int cIdx = cy * cW + cx;
            const QRgb rgb = yuvToRgb(yPlane[yIdx], uPlane[cIdx], vPlane[cIdx]);
            dst[i * 3 + 0] = qRed(rgb);
            dst[i * 3 + 1] = qGreen(rgb);
            dst[i * 3 + 2] = qBlue(rgb);
        }
    }
    return true;
}

bool loadYuv420p(const QByteArray& raw, int w, int h, LoadedImage& out, QString& err) {
    if (w <= 0 || h <= 0 || (w % 2) != 0 || (h % 2) != 0) {
        err = "YUV420 requires valid even width and height.";
        return false;
    }

    const int ySize = w * h;
    const int uvSize = (w / 2) * (h / 2);
    const int expected = ySize + uvSize * 2;
    if (raw.size() < expected) {
        err = QString("YUV420 file too small, expected at least %1 bytes.").arg(expected);
        return false;
    }

    out.yuv.width = w;
    out.yuv.height = h;
    out.yuv.subsampling = ChromaSubsampling::Cs420;
    out.yuv.y = raw.left(ySize);
    out.yuv.u = raw.mid(ySize, uvSize);
    out.yuv.v = raw.mid(ySize + uvSize, uvSize);
    return buildDisplayImageFromYuv(out.yuv, out.image, err);
}

bool loadYuv444p(const QByteArray& raw, int w, int h, LoadedImage& out, QString& err) {
    if (w <= 0 || h <= 0) {
        err = "YUV444 requires valid width and height.";
        return false;
    }

    const int planeSize = w * h;
    const int expected = planeSize * 3;
    if (raw.size() < expected) {
        err = QString("YUV444 file too small, expected at least %1 bytes.").arg(expected);
        return false;
    }

    out.yuv.width = w;
    out.yuv.height = h;
    out.yuv.subsampling = ChromaSubsampling::Cs444;
    out.yuv.y = raw.left(planeSize);
    out.yuv.u = raw.mid(planeSize, planeSize);
    out.yuv.v = raw.mid(planeSize * 2, planeSize);
    return buildDisplayImageFromYuv(out.yuv, out.image, err);
}

bool loadNv12(const QByteArray& raw, int w, int h, LoadedImage& out, QString& err) {
    if (w <= 0 || h <= 0 || (w % 2) != 0 || (h % 2) != 0) {
        err = "NV12 requires valid even width and height.";
        return false;
    }

    const int ySize = w * h;
    const int uvInterleavedSize = w * (h / 2);
    const int uvPlaneSize = (w / 2) * (h / 2);
    const int expected = ySize + uvInterleavedSize;
    if (raw.size() < expected) {
        err = QString("NV12 file too small, expected at least %1 bytes.").arg(expected);
        return false;
    }

    out.yuv.width = w;
    out.yuv.height = h;
    out.yuv.subsampling = ChromaSubsampling::Cs420;
    out.yuv.y = raw.left(ySize);
    out.yuv.u.resize(uvPlaneSize);
    out.yuv.v.resize(uvPlaneSize);

    const uchar* uv = reinterpret_cast<const uchar*>(raw.constData() + ySize);
    uchar* u = reinterpret_cast<uchar*>(out.yuv.u.data());
    uchar* v = reinterpret_cast<uchar*>(out.yuv.v.data());
    for (int j = 0; j < h / 2; ++j) {
        for (int i = 0; i < w / 2; ++i) {
            const int srcIdx = j * w + i * 2;
            const int dstIdx = j * (w / 2) + i;
            u[dstIdx] = uv[srcIdx];
            v[dstIdx] = uv[srcIdx + 1];
        }
    }

    return buildDisplayImageFromYuv(out.yuv, out.image, err);
}

bool loadNv16(const QByteArray& raw, int w, int h, LoadedImage& out, QString& err) {
    if (w <= 0 || h <= 0 || (w % 2) != 0) {
        err = "NV16 requires valid width and height, and width must be even.";
        return false;
    }

    const int ySize = w * h;
    const int uvInterleavedSize = w * h;
    const int uvPlaneSize = (w / 2) * h;
    const int expected = ySize + uvInterleavedSize;
    if (raw.size() < expected) {
        err = QString("NV16 file too small, expected at least %1 bytes.").arg(expected);
        return false;
    }

    out.yuv.width = w;
    out.yuv.height = h;
    out.yuv.subsampling = ChromaSubsampling::Cs422;
    out.yuv.y = raw.left(ySize);
    out.yuv.u.resize(uvPlaneSize);
    out.yuv.v.resize(uvPlaneSize);

    const uchar* uv = reinterpret_cast<const uchar*>(raw.constData() + ySize);
    uchar* u = reinterpret_cast<uchar*>(out.yuv.u.data());
    uchar* v = reinterpret_cast<uchar*>(out.yuv.v.data());
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w / 2; ++i) {
            const int srcIdx = j * w + i * 2;
            const int dstIdx = j * (w / 2) + i;
            u[dstIdx] = uv[srcIdx];
            v[dstIdx] = uv[srcIdx + 1];
        }
    }

    return buildDisplayImageFromYuv(out.yuv, out.image, err);
}

bool loadRgbAndConvertToBt702Yuv(const QString& path, LoadedImage& out, QString& err) {
    QImage img(path);
    if (img.isNull()) {
        err = "Failed to read image. Check file path and format.";
        return false;
    }

    out.image = img.convertToFormat(QImage::Format_RGB888);
    out.yuv.width = out.image.width();
    out.yuv.height = out.image.height();
    out.yuv.subsampling = ChromaSubsampling::Cs444;

    const int size = out.yuv.width * out.yuv.height;
    out.yuv.y.resize(size);
    out.yuv.u.resize(size);
    out.yuv.v.resize(size);

    uchar* yPlane = reinterpret_cast<uchar*>(out.yuv.y.data());
    uchar* uPlane = reinterpret_cast<uchar*>(out.yuv.u.data());
    uchar* vPlane = reinterpret_cast<uchar*>(out.yuv.v.data());

    for (int j = 0; j < out.image.height(); ++j) {
        const uchar* row = out.image.constScanLine(j);
        for (int i = 0; i < out.image.width(); ++i) {
            const int idx = j * out.image.width() + i;
            const int base = i * 3;
            int y = 0;
            int u = 0;
            int v = 0;
            rgbToYuvBt702(row[base], row[base + 1], row[base + 2], y, u, v);
            yPlane[idx] = static_cast<uchar>(y);
            uPlane[idx] = static_cast<uchar>(u);
            vPlane[idx] = static_cast<uchar>(v);
        }
    }

    return true;
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
} // namespace

bool ImageLoader::load(const ImageSource& src, LoadedImage& out, QString& err) {
    out = LoadedImage{};

    if (src.path.isEmpty()) {
        err = "Empty path.";
        return false;
    }

    const PixelFormat fmt = detectFormat(src);
    out.format = fmt;

    if (fmt == PixelFormat::PngJpg) {
        return loadRgbAndConvertToBt702Yuv(src.path, out, err);
    }

    int width = src.width;
    int height = src.height;
    if (src.format == PixelFormat::Auto) {
        int parsedW = 0;
        int parsedH = 0;
        if (parseResolutionFromFileName(src.path, parsedW, parsedH)) {
            width = parsedW;
            height = parsedH;
        }
    }

    QFile file(src.path);
    if (!file.open(QIODevice::ReadOnly)) {
        err = QString("Failed to open file: %1").arg(src.path);
        return false;
    }
    const QByteArray raw = file.readAll();

    switch (fmt) {
    case PixelFormat::YUV420P:
        return loadYuv420p(raw, width, height, out, err);
    case PixelFormat::YUV444P:
        return loadYuv444p(raw, width, height, out, err);
    case PixelFormat::NV12:
        return loadNv12(raw, width, height, out, err);
    case PixelFormat::NV16:
        return loadNv16(raw, width, height, out, err);
    case PixelFormat::Auto:
    case PixelFormat::PngJpg:
        break;
    }

    err = "Unsupported format.";
    return false;
}
