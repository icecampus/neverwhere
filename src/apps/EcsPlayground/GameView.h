#pragma once

#include <QQuickFramebufferObject>
#include <QQuickWindow>
#include <QSGNode>

class EcsModel;

class GameView : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EcsModel* model READ model WRITE setModel NOTIFY modelChanged)

public:
    GameView();
    
    EcsModel* model() const;
    void setModel(EcsModel* model);

    Renderer *createRenderer() const override;

signals:
    void modelChanged();

private:
    EcsModel* m_model = nullptr;
};
