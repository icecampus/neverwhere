#include "chapters.h"

#include <fstream>
// #include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
const std::string INDEX_FILENAME = "index.json";

namespace
{
    BaseData::Chapter::Data loadChapterData(const std::filesystem::path& chapterDataPath) 
    {
        if (!std::filesystem::exists(chapterDataPath)) 
        {
            //spdlog::error("File not found: " + chapterDataPath.string());
        }

        std::ifstream file(chapterDataPath);
        if (!file.is_open()) 
        {
            //spdlog::error("Failed to open file: " + chapterDataPath.string());
        }

        try 
        {
            nlohmann::json j;
            file >> j;

            return j.get<BaseData::Chapter::Data>();
        }
        catch (const nlohmann::json::exception& e) 
        {
            //spdlog::error("JSON parsing error: " + std::string(e.what()));
        }

        return {};
    }

    
    void saveChapterData(const std::filesystem::path& chapterDataPath, const BaseData::Chapter::Data& data) 
    {
        std::ofstream file(chapterDataPath);
        if (!file.is_open()) 
        {
            //spdlog::error("Failed to create file: " + chapterDataPath.string());
        }

        try 
        {
            nlohmann::json j = data;
            file << std::setw(4) << j; 
        }
        catch (const nlohmann::json::exception& e) 
        {
            //spdlog::error("JSON serialization error: " + std::string(e.what()));
        }
    }
}

namespace BaseData
{

std::vector<MapDescription> loadMapsDescriptions(const std::filesystem::path& mapsPath)
{
    std::vector<MapDescription> restult;
    for (const auto& entry : fs::directory_iterator(mapsPath))
    {
        if (!entry.is_directory())
        {
            continue;
        }

        restult.push_back({ entry.path() });
    }

    return restult;
}

//Chapter
std::filesystem::path Chapter::thumbnail() const
{
    return indexPath.parent_path() / data.thumbnail;
}

//Chaptets
Chaptets Chaptets::load(const std::filesystem::path& chaptersPath)
{
    Chaptets result;
    if (fs::exists(chaptersPath))
    {
        for (const auto& entry : fs::directory_iterator(chaptersPath))
        {
            if (!entry.is_directory())
            {
                continue;
            }

            Chapter chapter;
            chapter.indexPath = entry.path() / INDEX_FILENAME;
            chapter.data = loadChapterData(chapter.indexPath);

            std::filesystem::path mapsPath = entry.path() / "maps";
            chapter.maps = loadMapsDescriptions(mapsPath);

            result.push_back(chapter);
        }

    }
    else
    {
        //spdlog::error("Chapters directory does not exist: {}", chaptersPath.string() );
    }

    return result;
}

void Chaptets::save(const Chaptets& chapters, const std::filesystem::path& chaptersPath)
{

}

}//