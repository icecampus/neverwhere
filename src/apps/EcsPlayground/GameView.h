#pragma once

#include <QQuickItem>
#include <QQuickWindow>
#include <QSGNode>

class EcsModel;

class GameView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EcsModel* model READ model WRITE setModel NOTIFY modelChanged)

public:
    GameView();
    
    EcsModel* model() const;
    void setModel(EcsModel* model);

signals:
    void modelChanged();

public slots:
    void sync();
    void cleanup();
    void handleWindowChanged(QQuickWindow *win);

private:
    void setupSokol();
    bool m_sokolInitialized = false;
    EcsModel* m_model = nullptr;
};
