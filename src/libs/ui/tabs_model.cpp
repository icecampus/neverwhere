#include "tabs_model.h"
#include <QQmlEngine>


// TabElement
TabElement::TabElement(
    TabType::Type type_,
    const QString& name_,
    bool fixed_,
    const QVariantMap& data_,
    const QVariantMap& extraData_,
    const QString& color_,
    const QString& icon_,
    QQuickItem* uiItem,
    QObject* parent)
    : QObject(parent)
    , type(type_)
    , name(name_)
    , fixed(fixed_)
    , data(data_)
    , extraData(extraData_)
    , icon(icon_)
    , color(color_)
    , item(uiItem)
{

}

PinnedState::Type TabElement::getPinned() const
{
    return pinned;
}

void TabElement::setPinned(PinnedState::Type state)
{
    if (pinned != state)
    {
        pinned = state;
        emit pinnedChanged();
    }
}

TabType::Type TabElement::getType() const
{
    return type;
}

QString TabElement::getName() const
{
    return name;
}

void TabElement::setName(QString name_)
{
    if (name != name_)
    {
        name = name_;
        emit nameChanged();
    }
}

bool TabElement::getFixed() const
{
    return fixed;
}

QVariantMap TabElement::getData() const
{
    return data;
}

QVariantMap TabElement::getExtraData() const
{
    return extraData;
}

void TabElement::setExtraData(QVariantMap extraData_)
{
    extraData = extraData_;
    emit extraDataChanged(extraData);
}

QString TabElement::getIcon() const
{
    return icon;
}

QString TabElement::getColor() const
{
    return color;
}

bool TabElement::getDirty() const
{
    return dirty;
}

void TabElement::setDirty(bool dirty_)
{
    if (dirty != dirty_)
    {
        dirty = dirty_;
        emit dirtyChanged();
    }
}

QQuickItem* TabElement::getItem() const
{
    return item;
}

void TabElement::setItem(QQuickItem* _item)
{
    if (item != _item)
    {
        item = _item;
        emit itemChanged();
    }
}

void TabElement::sendPrepareToBeRemoved()
{
    emit prepareToBeRemoved();
}

void TabElement::sendPrepareToChangeWindow()
{
    emit prepareToChangeWindow();
}

void TabElement::activate()
{
    emit activated();
}

// TabsModel
TabsModel::TabsModel(QObject* parent) : QAbstractListModel(parent)
{
    elements.emplace_back(new TabElement(
        TabType::Home,
        "Home",
        true,
        QVariantMap(),
        QVariantMap(),
        "#4287f5",
        "qrc:/tab_home.png",
        nullptr,
        this));
}

int TabsModel::getCount()
{
    return rowCount(QModelIndex());
}

QObject* TabsModel::getCreator()
{
    return creator;
}

void TabsModel::setCreator(QObject* creator_)
{
    if (creator != creator_)
    {
        creator = creator_;
        emit creatorChanged();
    }
}

int TabsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        return 0;
    }
    return (int)elements.size();
}

QVariant TabsModel::data(const QModelIndex& index, int role) const
{
    if (index.isValid())
    {
        switch (role)
        {
        case ItemRole:
        {
            return QVariant::fromValue(elements[index.row()].get());
        }
        case TypeRole:
        {
            return QVariant::fromValue(elements[index.row()]->getType());
        }
        }
    }

    return QVariant();
}

QHash<int, QByteArray> TabsModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[ItemRole] = "element";
    roles[TypeRole] = "type";

    return roles;
}

namespace
{
    // проверка на возможность сериализации data
    bool trySerialization(const QVariantMap& data)
    {
        QByteArray byteArr;
        QDataStream write(&byteArr, QIODevice::WriteOnly);
        write << data;

        QDataStream read(&byteArr, QIODevice::ReadOnly);
        QVariantMap restored;
        read >> restored;

        return restored == data;
    }
} // namespace

bool TabsModel::add(TabType::Type type, QString name, bool fixed, QVariantMap data, QVariantMap extraData)
{
    if (trySerialization(data))
    {
        if (getIndexByName(name) < 0)
        {
            QVariant returnedValue;
            QMetaObject::invokeMethod(
                creator,
                "createContentItem",
                Q_RETURN_ARG(QVariant, returnedValue),
                Q_ARG(QVariant, QVariant(type)));

            QQuickItem* uiItem = returnedValue.value<QQuickItem*>();

            beginInsertRows(QModelIndex(), static_cast<int>(elements.size()), static_cast<int>(elements.size()));
            elements.emplace_back(new TabElement(type, name, fixed, data, extraData, "orange", "", uiItem, this));
            QQmlEngine::setObjectOwnership(elements.back().get(), QQmlEngine::CppOwnership);
            endInsertRows();
            emit countChanged();
            return true;
        }
        else
        {
            qDebug() << "Try add already exist tab: " << name;
        }
    }
    else
    {
        qDebug() << "Can't serialize tab <data>";
    }

    return false;
}

void TabsModel::remove(int index)
{
    removeQueries.push_back(index);

    QQuickItem* contentItem = elements[index]->getItem();

    beginRemoveRows(QModelIndex(), index, index);
    elements.erase(elements.begin() + index);
    endRemoveRows();

    contentItem->deleteLater();

    emit countChanged();
}

bool TabsModel::move(int from, int to)
{
    normalizeIndexes(from, to);

    const QModelIndex fromIndex = index(from);
    const QModelIndex toIndex = index(to);

    if (!fromIndex.isValid() || !toIndex.isValid())
    {
        return false;
    }

    if (from + 1 == to) // read carefully https://doc.qt.io/qt-6/qabstractitemmodel.html#beginMoveRows
    {
        std::swap(from, to);
    }

    const auto moved = beginMoveRows(QModelIndex(), from, from, QModelIndex(), to);
    if (moved)
    {
        std::swap(elements[from], elements[to]);
        endMoveRows();
        return true;
    }

    return false;
}

int TabsModel::getIndexByName(QString name)
{
    int result = -1;
    for (int i = 0; i < elements.size(); i++)
    {
        if (elements[i]->getName() == name)
        {
            result = i;
            break;
        }
    }
    return result;
}

QVariant TabsModel::getElementByIndex(int index, bool tabbedOnly)
{
    if (tabbedOnly)
    {
        int count = 0;
        for (const auto& element : elements)
        {
            if (element->getPinned() == PinnedState::Type::Tabbed)
            {
                if (count == index)
                {
                    return QVariant::fromValue(element.get());
                }
                ++count;
            }
        }
    }
    return QVariant::fromValue(elements.at(index).get());
}

QVariant TabsModel::getElementByName(QString name)
{
    int index = getIndexByName(name);
    if (index > 0)
    {
        return getElementByIndex(index);
    }

    return QVariant();
}

const TabElement* TabsModel::getElement(int index) const
{
    return elements.at(index).get();
}

void TabsModel::normalizeIndexes(int& from, int& to)
{
    int pinnedCountBeforeFrom = 0;
    int pinnedCountBeforeTo = 0;

    for (int i = 0, unpinnedIndex = 0; i < elements.size(); ++i)
    {
        if (elements[i]->getPinned() == PinnedState::Type::SeparateWindow)
        {
            if (unpinnedIndex <= from)
            {
                ++pinnedCountBeforeFrom;
            }
            if (unpinnedIndex <= to)
            {
                ++pinnedCountBeforeTo;
            }
        }
        else
        {
            ++unpinnedIndex;
        }
    }

    from += pinnedCountBeforeFrom;
    to += pinnedCountBeforeTo;

    from = std::min(from, static_cast<int>(elements.size()) - 1);
    to = std::min(to, static_cast<int>(elements.size()) - 1);
}



//TabsModel
const TabsModel::Elements& TabsModel::getElements() const
{
    return elements;
}
