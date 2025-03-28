#include "chapters_image_provider.h"

#include <QImage>
#include <QPainter>
#include <QColor>
#include <QPolygonF>
#include <QPointF>
#include <QRandomGenerator>
#include <QVector>
#include <QString>

#include <QImage>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QPainter>
#include <QGradient>
#include <QVector>
namespace
{

    QImage generateRandomImage(const QString& seedString) {
        const int SIZE = 128;
        QImage image(SIZE, SIZE, QImage::Format_RGB32);

        // Генерация seed из строки
        uint32_t seed = qHash(seedString);
        QRandomGenerator rng(seed);

        // Параметры волн
        auto genParam = [&](float min, float max) -> float {
            return rng.bounded(1000) / 1000.0f * (max - min) + min;
            };

        // Параметры для цветовых каналов
        struct {
            float angle, freq, amp;
            QVector2D direction;
        } rWave, gWave, bWave;

        // Инициализация параметров волн
        auto initWave = [&](auto& wave) {
            wave.angle = genParam(0, 2 * M_PI);
            wave.freq = genParam(0.02f, 0.15f);
            wave.amp = genParam(0.4f, 1.0f);
            wave.direction = QVector2D(cos(wave.angle), sin(wave.angle));
            };

        initWave(rWave);
        initWave(gWave);
        initWave(bWave);

        // Генерация шума
        for (int y = 0; y < SIZE; ++y) {
            for (int x = 0; x < SIZE; ++x) {
                // Нормализованные координаты
                float nx = x / float(SIZE) * 2 - 1;
                float ny = y / float(SIZE) * 2 - 1;

                // Вычисление волновых паттернов
                auto calc = [](float pos, auto& wave) {
                    return qSin(wave.direction.x() * pos * wave.freq * 20 +
                        wave.direction.y() * pos * wave.freq * 20) * wave.amp;
                    };

                float r = calc(nx, rWave) + calc(ny, rWave);
                float g = calc(nx, gWave) + calc(ny, gWave);
                float b = calc(nx, bWave) + calc(ny, bWave);

                // Нормализация и преобразование в цвет
                auto toColor = [](float val) {
                    return static_cast<uint8_t>((val * 0.5f + 0.5f) * 255);
                    };

                image.setPixelColor(x, y, QColor(
                    toColor(r * 0.8f + g * 0.2f),
                    toColor(g * 0.8f + b * 0.2f),
                    toColor(b * 0.8f + r * 0.2f)
                ));
            }
        }

        return image;
    }

}

QImage ChaptersImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) 
{
    QImage& image = m_imageCache[id];

    if (image.isNull())
    {
        image = generateRandomImage(id);
    }

    if (size)
    {
        *size = image.size();
    }

    if (requestedSize.isValid()) 
    {
        return image.scaled(requestedSize, Qt::KeepAspectRatio);
    }
    return image;
}
