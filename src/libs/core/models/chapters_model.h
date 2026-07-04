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

    // Create a brand-new chapter on disk under the chapters directory passed
    // to load(): writes <chaptersPath>/<safeName>/index.json and an empty
    // <chaptersPath>/<safeName>/maps/map.json, registers the chapter in this
    // model and returns the new Chapter* (so QML can open it in a Workspace).
    // Returns nullptr if the directory could not be created.
    Q_INVOKABLE Chapter* createChapter(const QString& name);

protected:
    void processElement(Chapter& element) override;

private:
    std::map<QUuid, Chapter*> uuid2Chapter;
    std::filesystem::path chaptersPath;
};