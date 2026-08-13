#pragma once
#include "game_object.h"

// Fence piece object (fence3d asset, FenceLandscape layer): a post (kind 0,
// 1 cell at `position`) or a section (kind 1, `length` cells along
// (axisX,axisY) starting at `position`). Endpoint links and fence components
// are derived state (fence_core::FenceModel) — only these flat fields persist.
class Fence : public GameObject
{
    Q_OBJECT

    Q_PROPERTY(int kind READ getKind NOTIFY fenceChanged)
    Q_PROPERTY(int axisX READ getAxisX NOTIFY fenceChanged)
    Q_PROPERTY(int axisY READ getAxisY NOTIFY fenceChanged)
    Q_PROPERTY(int length READ getLength NOTIFY fenceChanged)

public:
    explicit Fence(QObject* parent);

    void load(const BaseData::GameObject& data) override;

    int getKind() const;
    int getAxisX() const;
    int getAxisY() const;
    int getLength() const;
    void setPiece(int kind, int axisX, int axisY, int length);

signals:
    void fenceChanged();
};
