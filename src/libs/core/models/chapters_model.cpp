#include "chapters_model.h"
#include "base.h"

#include <magic_enum/magic_enum.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace fs = std::filesystem;

namespace
{
    // Strip characters that are illegal in a Windows path component so that
    // a user-typed chapter name becomes a safe directory name.
    QString sanitizeChapterName(const QString& name)
    {
        QString safe;
        for (const QChar& ch : name)
        {
            if (ch == QLatin1Char('/') || ch == QLatin1Char('\\')
                || ch == QLatin1Char(':') || ch == QLatin1Char('*')
                || ch == QLatin1Char('?') || ch == QLatin1Char('"')
                || ch == QLatin1Char('<') || ch == QLatin1Char('>')
                || ch == QLatin1Char('|'))
            {
                safe += QLatin1Char('_');
            }
            else
            {
                safe += ch;
            }
        }
        if (safe.trimmed().isEmpty())
        {
            safe = QStringLiteral("Chapter");
        }
        return safe;
    }

    // Return a chapter directory name that does not exist yet under
    // chaptersRoot, appending _2, _3, ... on collisions.
    fs::path makeUniqueChapterDir(const fs::path& chaptersRoot, const QString& name)
    {
        const std::string base = sanitizeChapterName(name).toStdString();
        fs::path candidate = chaptersRoot / base;
        if (!fs::exists(candidate))
        {
            return candidate;
        }

        for (int suffix = 2; ; ++suffix)
        {
            candidate = chaptersRoot / (base + "_" + std::to_string(suffix));
            if (!fs::exists(candidate))
            {
                return candidate;
            }
        }
    }
}

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
    model.chaptersPath = chaptersPath;

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
    Chapter* chapter = uuid2Chapter.at(chapterUuid);
    return chapter;
}

void ChaptersModel::processElement(Chapter& chapter)
{
    if (!uuid2Chapter.count(chapter.getUuid()))
    {
        uuid2Chapter[chapter.getUuid()] = &chapter;
    }

}

Chapter* ChaptersModel::createChapter(const QString& name)
{
    if (chaptersPath.empty())
    {
        qWarning() << "ChaptersModel::createChapter: chaptersPath is empty";
        return nullptr;
    }

    try
    {
        fs::path chapterDir = makeUniqueChapterDir(chaptersPath, name);
        fs::create_directories(chapterDir);
        fs::create_directories(chapterDir / "maps");

        BaseData::Chapter::Data data;
        data.uuid = boost::uuids::random_generator()();
        data.name = sanitizeChapterName(name).toStdString();
        data.thumbnail = "thumbnail.png";

        fs::path indexPath = chapterDir / "index.json";
        BaseData::Chapter::saveChapterData(indexPath, data);

        // Empty map: initialize every layer so Map::save's `.at(layerType)`
        // does not throw on a fresh chapter.
        BaseData::Map emptyMap;
        for (const LayerTypes::Type layerType : magic_enum::enum_values<LayerTypes::Type>())
        {
            emptyMap[layerType] = BaseData::Layer{};
        }
        BaseData::Map::save(emptyMap, chapterDir / "maps" / "map.json");

        BaseData::Chapter chapterData;
        chapterData.data = data;
        chapterData.indexPath = indexPath;

        auto chapter = std::make_unique<Chapter>(this);
        chapter->load(chapterData);
        Chapter* raw = chapter.get();
        addElement<Chapter>(std::move(chapter));

        qInfo().noquote() << "ChaptersModel::createChapter created" << QString::fromStdString(data.name)
                          << "at" << QString::fromStdString(chapterDir.string());

        return raw;
    }
    catch (const std::exception& e)
    {
        qWarning() << "ChaptersModel::createChapter exception:" << e.what();
        return nullptr;
    }
}

