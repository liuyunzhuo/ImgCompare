#include "ImageLoader.h"

#include <QFile>
#include <QFileInfo>

namespace {
int clampToByte(int v) {
    return qBound(0, v, 255);
}

QRgb yuvToRgb(int y, int u, int v) {
    const int c = y - 16;
    const int d = u - 128;
    const int e = v - 128;

    const int r = (298 * c + 409 * e + 128) >> 8;
    const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    const int b = (298 * c + 516 * d + 128) >> 8;
    return qRgb(clampToByte(r), clampToByte(g), clampToByte(b));
}

PixelFormat detectFormat(const ImageSource& src) {
    if (src.format != PixelFormat::Auto) {
        return src.format;
    }

    const QString ext = QFileInfo(src.path).suffix().toLower();
    if (ext == "png" || ext == "jpg" || ext == "jpeg") {
        return PixelFormat::PngJpg;
    }
    if (ext == "yuv420" || ext == "i420") {
        return PixelFormat::YUV420P;
    }
    if (ext == "yuv444") {
        return PixelFormat::YUV444P;
    }
    if (ext == "nv12") {
        return PixelFormat::NV12;
    }
    if (ext == "nv16") {
        return PixelFormat::NV16;
    }

    return PixelFormat::PngJpg;
}

bool loadYuv420p(const QByteArray& raw, int w, int h, QImage& outImage, QString& err) {
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

    const uchar* yPlane = reinterpret_cast<const uchar*>(raw.constData());
    const uchar* uPlane = yPlane + ySize;
    const uchar* vPlane = uPlane + uvSize;

    outImage = QImage(w, h, QImage::Format_RGB888);
    for (int j = 0; j < h; ++j) {
        uchar* dst = outImage.scanLine(j);
        for (int i = 0; i < w; ++i) {
            const int y = yPlane[j * w + i];
            const int u = uPlane[(j / 2) * (w / 2) + (i / 2)];
            const int v = vPlane[(j / 2) * (w / 2) + (i / 2)];
            const QRgb rgb = yuvToRgb(y, u, v);
            dst[i * 3 + 0] = qRed(rgb);
            dst[i * 3 + 1] = qGreen(rgb);
            dst[i * 3 + 2] = qBlue(rgb);
        }
    }
    return true;
}

bool loadYuv444p(const QByteArray& raw, int w, int h, QImage& outImage, QString& err) {
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

    const uchar* yPlane = reinterpret_cast<const uchar*>(raw.constData());
    const uchar* uPlane = yPlane + planeSize;
    const uchar* vPlane = uPlane + planeSize;

    outImage = QImage(w, h, QImage::Format_RGB888);
    for (int j = 0; j < h; ++j) {
        uchar* dst = outImage.scanLine(j);
        for (int i = 0; i < w; ++i) {
            const int idx = j * w + i;
            const QRgb rgb = yuvToRgb(yPlane[idx], uPlane[idx], vPlane[idx]);
            dst[i * 3 + 0] = qRed(rgb);
            dst[i * 3 + 1] = qGreen(rgb);
            dst[i * 3 + 2] = qBlue(rgb);
        }
    }
    return true;
}

bool loadNv12(const QByteArray& raw, int w, int h, QImage& outImage, QString& err) {
    if (w <= 0 || h <= 0 || (w % 2) != 0 || (h % 2) != 0) {
        err = "NV12 requires valid even width and height.";
        return false;
    }

    const int ySize = w * h;
    const int uvSize = w * (h / 2);
    const int expected = ySize + uvSize;
    if (raw.size() < expected) {
        err = QString("NV12 file too small, expected at least %1 bytes.").arg(expected);
        return false;
    }

    const uchar* yPlane = reinterpret_cast<const uchar*>(raw.constData());
    const uchar* uvPlane = yPlane + ySize;

    outImage = QImage(w, h, QImage::Format_RGB888);
    for (int j = 0; j < h; ++j) {
        uchar* dst = outImage.scanLine(j);
        const int uvRow = (j / 2) * w;
        for (int i = 0; i < w; ++i) {
            const int y = yPlane[j * w + i];
            const int uvIdx = uvRow + (i & ~1);
            const int u = uvPlane[uvIdx];
            const int v = uvPlane[uvIdx + 1];
            const QRgb rgb = yuvToRgb(y, u, v);
            dst[i * 3 + 0] = qRed(rgb);
            dst[i * 3 + 1] = qGreen(rgb);
            dst[i * 3 + 2] = qBlue(rgb);
        }
    }
    return true;
}

bool loadNv16(const QByteArray& raw, int w, int h, QImage& outImage, QString& err) {
    if (w <= 0 || h <= 0 || (w % 2) != 0) {
        err = "NV16 requires valid width and height, and width must be even.";
        return false;
    }

    const int ySize = w * h;
    const int uvSize = w * h;
    const int expected = ySize + uvSize;
    if (raw.size() < expected) {
        err = QString("NV16 file too small, expected at least %1 bytes.").arg(expected);
        return false;
    }

    const uchar* yPlane = reinterpret_cast<const uchar*>(raw.constData());
    const uchar* uvPlane = yPlane + ySize;

    outImage = QImage(w, h, QImage::Format_RGB888);
    for (int j = 0; j < h; ++j) {
        uchar* dst = outImage.scanLine(j);
        const int uvRow = j * w;
        for (int i = 0; i < w; ++i) {
            const int y = yPlane[j * w + i];
            const int uvIdx = uvRow + (i & ~1);
            const int u = uvPlane[uvIdx];
            const int v = uvPlane[uvIdx + 1];
            const QRgb rgb = yuvToRgb(y, u, v);
            dst[i * 3 + 0] = qRed(rgb);
            dst[i * 3 + 1] = qGreen(rgb);
            dst[i * 3 + 2] = qBlue(rgb);
        }
    }
    return true;
}
} // namespace

bool ImageLoader::load(const ImageSource& src, QImage& outImage, QString& err) {
    if (src.path.isEmpty()) {
        err = "Empty path.";
        return false;
    }

    const PixelFormat fmt = detectFormat(src);
    if (fmt == PixelFormat::PngJpg) {
        QImage img(src.path);
        if (img.isNull()) {
            err = "Failed to read image. Check file path and format.";
            return false;
        }
        outImage = img.convertToFormat(QImage::Format_RGB888);
        return true;
    }

    QFile file(src.path);
    if (!file.open(QIODevice::ReadOnly)) {
        err = QString("Failed to open file: %1").arg(src.path);
        return false;
    }

    const QByteArray raw = file.readAll();

    switch (fmt) {
    case PixelFormat::YUV420P:
        return loadYuv420p(raw, src.width, src.height, outImage, err);
    case PixelFormat::YUV444P:
        return loadYuv444p(raw, src.width, src.height, outImage, err);
    case PixelFormat::NV12:
        return loadNv12(raw, src.width, src.height, outImage, err);
    case PixelFormat::NV16:
        return loadNv16(raw, src.width, src.height, outImage, err);
    case PixelFormat::Auto:
    case PixelFormat::PngJpg:
        break;
    }

    err = "Unsupported format.";
    return false;
}
