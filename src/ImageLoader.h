#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

enum class PixelFormat {
    Auto,
    PngJpg,
    YUV420P,
    YUV444P,
    NV12,
    NV16
};

struct ImageSource {
    QString path;
    PixelFormat format = PixelFormat::Auto;
    int width = 0;
    int height = 0;
};

enum class ChromaSubsampling {
    Cs444,
    Cs422,
    Cs420
};

struct YuvPlanes {
    int width = 0;
    int height = 0;
    ChromaSubsampling subsampling = ChromaSubsampling::Cs444;
    QByteArray y;
    QByteArray u;
    QByteArray v;
};

struct LoadedImage {
    QImage image;
    PixelFormat format = PixelFormat::Auto;
    YuvPlanes yuv;
};

class ImageLoader {
public:
    static bool load(const ImageSource& src, LoadedImage& out, QString& err);
};
