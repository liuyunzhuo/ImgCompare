#pragma once

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

class ImageLoader {
public:
    static bool load(const ImageSource& src, QImage& outImage, QString& err);
};
