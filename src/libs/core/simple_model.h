#pragma once
#include <QAbstractListModel>
#include <deque>
#include <memory>

template<typename Element>
class SimpleModel : public QAbstractListModel
{

public:
    enum Roles 
    {
        ElementRole = Qt::UserRole + 1
    };

    explicit SimpleModel(QObject *parent = nullptr) : 
        QAbstractListModel(parent) 
    {
    
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override 
    {
        if (parent.isValid())
            return 0;
        return static_cast<int>(_elements.size());
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override 
    {
        if (!index.isValid() || index.row() >= static_cast<int>(_elements.size()))
            return QVariant();

        if (role == ElementRole)
            return QVariant::fromValue(_elements[index.row()].get());

        return QVariant();
    }

    QHash<int, QByteArray> roleNames() const override 
    {
        QHash<int, QByteArray> roles;
        roles[ElementRole] = "element";
        return roles;
    }


    template <typename T, typename... Args>
    void addElement(Args&&... args) 
    {
        beginInsertRows(QModelIndex(), _elements.size(), _elements.size());
        _elements.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        endInsertRows();
    }

    Element *element(int index) const 
    {
        if (index >= 0 && index < static_cast<int>(_elements.size()))
            return _elements[index].get();
        return nullptr;
    }

    void clear() {
        beginResetModel();
        _elements.clear();
        endResetModel();
    }

private:
    std::deque<std::unique_ptr<Element>> _elements;
};

