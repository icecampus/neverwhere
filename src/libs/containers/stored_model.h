#pragma once

#pragma once
#include <QAbstractListModel>
#include <deque>
#include <memory>

template<typename Container>
class StoredModel : public QAbstractListModel
{

public:
    enum Roles
    {
        ElementRole = Qt::UserRole + 1
    };

    using ElementType = typename Container::value_type; 

    explicit StoredModel(QObject* parent);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    template <typename T, typename... Args>
    void addElement(Args&&... args)
    {
        beginInsertRows(QModelIndex(), _elements.size(), _elements.size());
        _elements.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        endInsertRows();

        processElement(*_elements.back().get());
    }

    template <typename T>
    void addElement(std::unique_ptr<T> element)
    {
        static_assert(std::is_base_of<ElementType, T>::value, "T must derive from Element");
        beginInsertRows(QModelIndex(), _elements.size(), _elements.size());
        _elements.push_back(std::move(element));
        endInsertRows();

        processElement(*_elements.back().get());
    }

    ElementType* element(int index) const;

    void clear();
    int size();

protected:
    virtual void processElement(ElementType& element) {}

private:
    Container _elements;
};

//Impl
template<typename Container>
StoredModel<Container>::StoredModel(QObject* parent) :
    QAbstractListModel(parent)
{

}
template<typename Container>
int StoredModel<Container>::rowCount(const QModelIndex& parent) const 
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(_elements.size());
}

template<typename Container>
QVariant StoredModel<Container>::data(const QModelIndex& index, int role) const 
{
    if (!index.isValid() || index.row() >= static_cast<int>(_elements.size()))
        return QVariant();

    if (role == ElementRole)
        return QVariant::fromValue(_elements[index.row()].get());

    return QVariant();
}

template<typename Container>
QHash<int, QByteArray> StoredModel<Container>::roleNames() const 
{
    QHash<int, QByteArray> roles;
    roles[ElementRole] = "element";
    return roles;
}

template<typename Container>
StoredModel<Container>::ElementType* StoredModel<Container>::element(int index) const
{
    if (index >= 0 && index < static_cast<int>(_elements.size()))
        return _elements[index].get();
    return nullptr;
}

template<typename Container>
void StoredModel<Container>::clear() 
{
    beginResetModel();
    _elements.clear();
    endResetModel();
}

template<typename Container>
int StoredModel<Container>::size()
{
    return _elements.size();
}

