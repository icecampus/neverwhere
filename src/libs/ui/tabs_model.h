#pragma once

#include <deque>
#include <QAbstractListModel>
#include <QQuickItem>
#include <QTimer>

namespace TabType
{
    Q_NAMESPACE;

    // TabType
    enum Type
    {
        Home,
        Workspace,
        Game,   // play-test tab: isolated game runtime on a chapter's map
    };
    Q_ENUM_NS(Type);
} // namespace TabType

namespace PinnedState
{
    Q_NAMESPACE;

    enum Type
    {
        Tabbed,
        SeparateWindow
    };
    Q_ENUM_NS(Type);
} // namespace PinnedState

class TabsModel;

// TabElement
class TabElement : public QObject
{
    Q_OBJECT;

    Q_PROPERTY(PinnedState::Type pinned READ getPinned WRITE setPinned NOTIFY pinnedChanged)
    Q_PROPERTY(TabType::Type type READ getType NOTIFY typeChanged);
    Q_PROPERTY(QString name READ getName WRITE setName NOTIFY nameChanged);
    Q_PROPERTY(bool fixed READ getFixed NOTIFY fixedChanged);
    Q_PROPERTY(QVariantMap data READ getData CONSTANT);
    Q_PROPERTY(QVariantMap extraData READ getExtraData WRITE setExtraData NOTIFY extraDataChanged);

    Q_PROPERTY(QString icon READ getIcon NOTIFY iconChanged);
    Q_PROPERTY(QString color READ getColor NOTIFY colorChanged);
    Q_PROPERTY(bool dirty READ getDirty WRITE setDirty NOTIFY dirtyChanged);

    Q_PROPERTY(QQuickItem* item READ getItem WRITE setItem NOTIFY itemChanged);

public:
    TabElement(
        TabType::Type type,
        const QString& name,
        bool fixed,
        const QVariantMap& data,
        const QVariantMap& extraData,
        const QString& color,
        const QString& icon,
        QQuickItem* uiItem,
        QObject* parent);

    // properties
    PinnedState::Type getPinned() const;
    void setPinned(PinnedState::Type state);

    TabType::Type getType() const;

    QString getName() const;
    void setName(QString name);

    bool getFixed() const;
    QVariantMap getData() const;
    QVariantMap getExtraData() const;
    void setExtraData(QVariantMap extraData);
    QString getIcon() const;
    QString getColor() const;

    bool getDirty() const;
    void setDirty(bool dirty);

    QQuickItem* getItem() const;
    void setItem(QQuickItem* _item);

    //
    Q_INVOKABLE void sendPrepareToBeRemoved();
    Q_INVOKABLE void sendPrepareToChangeWindow();
    Q_INVOKABLE void activate();
signals:
    void pinnedChanged();
    void typeChanged();
    void nameChanged();
    void fixedChanged();
    void extraDataChanged(QVariantMap extraData);
    void iconChanged();
    void colorChanged();
    void dirtyChanged();
    void itemChanged();

    //
    void prepareToBeRemoved();
    void prepareToChangeWindow();
    void activated();
    void afterPrepareToRemove();

private:
    PinnedState::Type pinned = PinnedState::Tabbed;
    TabType::Type type;
    QVariantMap data;
    QVariantMap extraData;
    QString name;
    QString icon;
    QString color;
    QQuickItem* item = nullptr;
    bool dirty = false;
    bool fixed;
};

// TabsModel
class TabsModel : public QAbstractListModel
{
    Q_OBJECT;

    Q_PROPERTY(int count READ getCount NOTIFY countChanged)
    Q_PROPERTY(QObject* creator READ getCreator WRITE setCreator NOTIFY creatorChanged)

public:
    using Element = std::unique_ptr<TabElement>;
    using Elements = std::deque<Element>;

    enum Roles
    {
        ItemRole = Qt::UserRole,
        TypeRole
    };

    TabsModel(QObject* parent = nullptr);

    // propeties
    int getCount();

    QObject* getCreator();
    void setCreator(QObject* creator);

    //
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual QHash<int, QByteArray> roleNames() const override;

    //
    Q_INVOKABLE bool add(TabType::Type type, QString name, bool fixed, QVariantMap data, QVariantMap extraData);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE bool move(int from, int to);

    Q_INVOKABLE int getIndexByName(QString name);
    Q_INVOKABLE QVariant getElementByName(QString name);
    Q_INVOKABLE QVariant getElementByIndex(int index, bool tabbedOnly = false);

    const Elements& getElements() const;
    const TabElement* getElement(int index) const;

signals:
    void countChanged();
    void creatorChanged();

private:
    void normalizeIndexes(int& from, int& to);

private:
    Elements elements;

    std::vector<int> removeQueries;
    QTimer timer;
    QObject* creator = nullptr;
};

Q_DECLARE_METATYPE(TabType::Type)
