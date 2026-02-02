#pragma once

#include <QQuickItem>
#include <QQuickWindow>
#include <QSGNode>

class GameView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    GameView();

signals:

public slots:
    void sync();
    void cleanup();
    void handleWindowChanged(QQuickWindow *win);

private:
    void setupSokol();
    bool m_sokolInitialized = false;
};
