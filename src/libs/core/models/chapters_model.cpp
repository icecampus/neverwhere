#include "chapters_model.h"
#include "base.h"

namespace fs = std::filesystem;

//Chapter
Chapter::Chapter(QObject* parent) :
    QObject(parent)
{

}

void Chapter::load(const BaseData::Chapter& data)
{
    chapterData = data;
}

BaseData::Chapter Chapter::data()
{
    return chapterData;
}

QString Chapter::name() const
{
    return chapterData.data.name.c_str();
}


QUuid Chapter::getUuid() const
{
    return base::boostUuidToQUuid(chapterData.data.uuid);
}

QString Chapter::getThumbnailPath() const
{
    auto thumbPath = chapterData.thumbnail();
    if (fs::exists(thumbPath))
    {
        return thumbPath.string().c_str();
    }
    
    return "";
}

QString Chapter::getMapPath() const
{
    auto mapPath = chapterData.thumbnail().parent_path() / "maps" / "map.json";
    return mapPath.string().c_str();
}

//LayersModel
ChaptersModel::ChaptersModel(QObject* parent):
    SimpleModel<Chapter>(parent)
{

}


void ChaptersModel::load(ChaptersModel& model, const std::filesystem::path& chaptersPath)
{
    BaseData::Chaptets chaptersData = BaseData::Chaptets::load(chaptersPath);

    for (const BaseData::Chapter& chapterData: chaptersData)
    {
        auto chapter = std::make_unique<Chapter>(&model);
        chapter->load(chapterData);

        model.addElement(std::move(chapter));
    }
}

Chapter* ChaptersModel::getByUuid(const QUuid& chapterUuid)
{
    return uuid2Chapter.at(chapterUuid);
}

void ChaptersModel::processElement(Chapter& chapter)
{
    if (!uuid2Chapter.count(chapter.getUuid()))
    {
        uuid2Chapter[chapter.getUuid()] = &chapter;
    }
    
}

