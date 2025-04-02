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
#include <QtEndian>

namespace
{



    QImage generateRandomImage(const QString& seedString) {
        // Вычисляем хеш SHA-1 от seedString для получения воспроизводимого seed
        QByteArray hash = QCryptographicHash::hash(seedString.toUtf8(), QCryptographicHash::Sha1);
        quint32 seed = qFromLittleEndian<quint32>(hash.constData()); // Берем первые 4 байта хеша как seed

        // Инициализируем генератор случайных чисел с полученным seed
        QRandomGenerator generator(seed);

        // Создаем изображение размером 300x150 с поддержкой альфа-канала
        QImage image(300, 150, QImage::Format_ARGB32);

        // Создаем объект для рисования на изображении
        QPainter painter(&image);

        // Генерируем три случайных цвета для градиента (RGB)
        QColor color1(generator.bounded(256), generator.bounded(256), generator.bounded(256));
        QColor color2(generator.bounded(256), generator.bounded(256), generator.bounded(256));
        QColor color3(generator.bounded(256), generator.bounded(256), generator.bounded(256));

        // Создаем линейный градиент от (0,0) к (300,150)
        QLinearGradient gradient(0, 0, 300, 150);
        gradient.setColorAt(0.0, color1); // Начало градиента
        gradient.setColorAt(0.5, color2); // Середина градиента
        gradient.setColorAt(1.0, color3); // Конец градиента

        // Устанавливаем градиент как кисть и рисуем прямоугольник на всё изображение
        painter.setBrush(gradient);
        painter.drawRect(0, 0, 300, 150);

        // Генерируем случайное количество кругов (от 1 до 3)
        int N = generator.bounded(1, 4); // bounded(1, 4) дает 1, 2 или 3

        // Рисуем N случайных кругов
        for (int i = 0; i < N; ++i) {
            // Генерируем радиус от 20 до 50 пикселей
            int radius = generator.bounded(20, 51);

            // Генерируем позицию центра круга так, чтобы он помещался в изображение
            int x = generator.bounded(radius, 301 - radius); // x от radius до 300 - radius
            int y = generator.bounded(radius, 151 - radius); // y от radius до 150 - radius

            // Генерируем случайный цвет с альфа-каналом от 100 до 200 для полупрозрачности
            QColor color(generator.bounded(256), generator.bounded(256), generator.bounded(256),
                generator.bounded(100, 201));

            // Устанавливаем цвет как кисть, убираем обводку и рисуем круг
            painter.setBrush(color);
            painter.setPen(Qt::NoPen); // Без обводки
            painter.drawEllipse(QPoint(x, y), radius, radius);
        }

        // Завершаем рисование
        painter.end();

        // Возвращаем готовое изображение
        return image;
    }

    //QImage generateRandomImage(const QString& seedString) {
    //    const int SIZE = 128;
    //    QImage image(SIZE, SIZE, QImage::Format_RGB32);

    //    // Генерация seed из строки
    //    uint32_t seed = qHash(seedString);
    //    QRandomGenerator rng(seed);

    //    // Параметры волн
    //    auto genParam = [&](float min, float max) -> float {
    //        return rng.bounded(1000) / 1000.0f * (max - min) + min;
    //        };

    //    // Параметры для цветовых каналов
    //    struct {
    //        float angle, freq, amp;
    //        QVector2D direction;
    //    } rWave, gWave, bWave;

    //    // Инициализация параметров волн
    //    auto initWave = [&](auto& wave) {
    //        wave.angle = genParam(0, 2 * M_PI);
    //        wave.freq = genParam(0.02f, 0.15f);
    //        wave.amp = genParam(0.4f, 1.0f);
    //        wave.direction = QVector2D(cos(wave.angle), sin(wave.angle));
    //        };

    //    initWave(rWave);
    //    initWave(gWave);
    //    initWave(bWave);

    //    // Генерация шума
    //    for (int y = 0; y < SIZE; ++y) {
    //        for (int x = 0; x < SIZE; ++x) {
    //            // Нормализованные координаты
    //            float nx = x / float(SIZE) * 2 - 1;
    //            float ny = y / float(SIZE) * 2 - 1;

    //            // Вычисление волновых паттернов
    //            auto calc = [](float pos, auto& wave) {
    //                return qSin(wave.direction.x() * pos * wave.freq * 20 +
    //                    wave.direction.y() * pos * wave.freq * 20) * wave.amp;
    //                };

    //            float r = calc(nx, rWave) + calc(ny, rWave);
    //            float g = calc(nx, gWave) + calc(ny, gWave);
    //            float b = calc(nx, bWave) + calc(ny, bWave);

    //            // Нормализация и преобразование в цвет
    //            auto toColor = [](float val) {
    //                return static_cast<uint8_t>((val * 0.5f + 0.5f) * 255);
    //                };

    //            image.setPixelColor(x, y, QColor(
    //                toColor(r * 0.8f + g * 0.2f),
    //                toColor(g * 0.8f + b * 0.2f),
    //                toColor(b * 0.8f + r * 0.2f)
    //            ));
    //        }
    //    }

    //    return image;
    //}

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
