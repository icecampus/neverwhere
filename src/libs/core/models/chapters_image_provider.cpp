#include "chapters_image_provider.h"

#include <QImage>
#include <QPainter>
#include <QColor>
#include <QPolygonF>
#include <QPointF>
#include <QRandomGenerator>
#include <QVector>
#include <QString>
#include <QCryptographicHash>
#include <QGradient>
#include <QtEndian>
#include <QPainterPath>

namespace
{



//QImage generateRandomImage(const QString& seedString) {
//    // Вычисляем хеш SHA-1 от seedString для получения воспроизводимого seed
//    QByteArray hash = QCryptographicHash::hash(seedString.toUtf8(), QCryptographicHash::Sha1);
//    quint32 seed = qFromLittleEndian<quint32>(hash.constData()); // Берем первые 4 байта хеша как seed
//
//    // Инициализируем генератор случайных чисел с полученным seed
//    QRandomGenerator generator(seed);
//
//    // Создаем изображение размером 300x150 с поддержкой альфа-канала
//    QImage image(300, 150, QImage::Format_ARGB32);
//
//    // Создаем объект для рисования на изображении
//    QPainter painter(&image);
//
//    // Генерируем три случайных цвета для градиента (RGB)
//    QColor color1(generator.bounded(256), generator.bounded(256), generator.bounded(256));
//    QColor color2(generator.bounded(256), generator.bounded(256), generator.bounded(256));
//    QColor color3(generator.bounded(256), generator.bounded(256), generator.bounded(256));
//
//    // Создаем линейный градиент от (0,0) к (300,150)
//    QLinearGradient gradient(0, 0, 300, 150);
//    gradient.setColorAt(0.0, color1); // Начало градиента
//    gradient.setColorAt(0.5, color2); // Середина градиента
//    gradient.setColorAt(1.0, color3); // Конец градиента
//
//    // Устанавливаем градиент как кисть и рисуем прямоугольник на всё изображение
//    painter.setBrush(gradient);
//    painter.drawRect(0, 0, 300, 150);
//
//    // Генерируем случайное количество кругов (от 1 до 3)
//    int N = generator.bounded(1, 4); // bounded(1, 4) дает 1, 2 или 3
//
//    // Рисуем N случайных кругов
//    for (int i = 0; i < N; ++i) {
//        // Генерируем радиус от 20 до 50 пикселей
//        int radius = generator.bounded(20, 51);
//
//        // Генерируем позицию центра круга так, чтобы он помещался в изображение
//        int x = generator.bounded(radius, 301 - radius); // x от radius до 300 - radius
//        int y = generator.bounded(radius, 151 - radius); // y от radius до 150 - radius
//
//        // Генерируем случайный цвет с альфа-каналом от 100 до 200 для полупрозрачности
//        QColor color(generator.bounded(256), generator.bounded(256), generator.bounded(256),
//            generator.bounded(100, 201));
//
//        // Устанавливаем цвет как кисть, убираем обводку и рисуем круг
//        painter.setBrush(color);
//        painter.setPen(Qt::NoPen); // Без обводки
//        painter.drawEllipse(QPoint(x, y), radius, radius);
//    }
//
//    // Завершаем рисование
//    painter.end();
//
//    // Возвращаем готовое изображение
//    return image;
//}

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

    // Генерируем три случайных цвета для градиента фона (RGB)
    QColor color1(generator.bounded(256), generator.bounded(256), generator.bounded(256));
    QColor color2(generator.bounded(256), generator.bounded(256), generator.bounded(256));
    QColor color3(generator.bounded(256), generator.bounded(256), generator.bounded(256));

    // Создаем линейный градиент от (0,0) к (300,150) для фона
    QLinearGradient gradient(0, 0, 300, 150);
    gradient.setColorAt(0.0, color1); // Начало градиента
    gradient.setColorAt(0.5, color2); // Середина градиента
    gradient.setColorAt(1.0, color3); // Конец градиента

    // Устанавливаем градиент как кисть и рисуем прямоугольник на всё изображение
    painter.setBrush(gradient);
    painter.drawRect(0, 0, 300, 150);

    // Генерируем случайное количество кругов (от 1 до 3)
    int N = generator.bounded(1, 4); // bounded(1, 4) дает 1, 2 или 3

    // Рисуем N случайных кругов с эффектом свечения
    for (int i = 0; i < N; ++i) {
        // Генерируем радиус от 20 до 50 пикселей
        int radius = generator.bounded(20, 51);

        // Генерируем позицию центра круга так, чтобы он помещался в изображение
        int x = generator.bounded(radius, 301 - radius); // x от radius до 300 - radius
        int y = generator.bounded(radius, 151 - radius); // y от radius до 150 - radius

        // Генерируем случайный цвет с альфа-каналом от 100 до 200 для круга
        QColor circleColor(generator.bounded(256), generator.bounded(256), generator.bounded(256),
            generator.bounded(100, 201));

        // Цвет свечения: тот же цвет, но с альфой в два раза меньше
        int glowAlpha = circleColor.alpha() / 2;
        QColor glowColor = circleColor;
        glowColor.setAlpha(glowAlpha);

        // Создаем радиальный градиент для свечения
        QRadialGradient glowGradient(x, y, radius * 1.5);
        glowGradient.setColorAt(0.0, QColor(0, 0, 0, 0));    // Прозрачный в центре
        glowGradient.setColorAt(0.666, glowColor);           // На 2/3 радиуса — цвет свечения
        glowGradient.setColorAt(1.0, QColor(0, 0, 0, 0));    // На краю прозрачный

        // Устанавливаем градиент как кисть и убираем обводку
        painter.setBrush(glowGradient);
        painter.setPen(Qt::NoPen);

        // Рисуем эллипс для свечения (в 1.5 раза больше радиуса круга)
        painter.drawEllipse(QPointF(x, y), radius * 1.5, radius * 1.5);

        // Устанавливаем цвет круга как кисть и рисуем сам круг
        painter.setBrush(circleColor);
        painter.drawEllipse(QPointF(x, y), radius, radius);
    }

    // Завершаем рисование
    painter.end();

    // Возвращаем готовое изображение
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
