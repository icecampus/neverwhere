#pragma once
#include "simple_model.h"
#include "base_data/lib.h"


struct Chapter: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QUuid uuid READ getUuid CONSTANT)
    Q_PROPERTY(QString mapPath READ getMapPath CONSTANT) // tmp
public:
    explicit Chapter(QObject* parent);

    void load(const BaseData::Chapter& data);
    BaseData::Chapter data();

    //properties
    QString name() const;
    QUuid getUuid() const;
    QString getThumbnailPath() const;

    QString getMapPath() const;
private:
    BaseData::Chapter chapterData;
};

//LayersModel
class ChaptersModel : public SimpleModel<Chapter>
{
    Q_OBJECT
public:
    explicit ChaptersModel(QObject* parent);

    static void load(ChaptersModel& model, const std::filesystem::path& chaptersPath);

    Q_INVOKABLE Chapter* getByUuid(const QUuid& chapterUuid);
protected:
    void processElement(Chapter& element) override;

private:
    std::map<QUuid, Chapter*> uuid2Chapter;

};