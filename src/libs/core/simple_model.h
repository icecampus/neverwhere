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

    explicit SimpleModel(QObject *parent) : 
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

        processElement(*_elements.back().get());
    }

    template <typename T>
    void addElement(std::unique_ptr<T> element)
    {
        static_assert(std::is_base_of<Element, T>::value, "T must derive from Element");
        beginInsertRows(QModelIndex(), _elements.size(), _elements.size());
        _elements.push_back(std::move(element));
        endInsertRows();

        processElement(*_elements.back().get());
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

    size_t size();

protected:
    virtual void processElement(Element& element){}

private:
    std::deque<std::unique_ptr<Element>> _elements;
};

template<typename Element>
size_t SimpleModel<Element>::size()
{
    return _elements.size();
}

