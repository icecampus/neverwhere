#pragma once
#include "boost_uuids_serialization.h"
#include <filesystem>

namespace BaseData
{
    //MapDescription
    struct MapDescription
    {
        struct Data
        {
            std::string name;

            NLOHMANN_DEFINE_TYPE_INTRUSIVE(Data, name)
        };

        std::filesystem::path indexPath;
    };

    //Chapter
    struct Chapter
    {
        struct Data
        {
            boost::uuids::uuid uuid;
            std::string name;
            std::string thumbnail;

            NLOHMANN_DEFINE_TYPE_INTRUSIVE(Data, uuid, name, thumbnail)
        };

        Data data;
        
        std::filesystem::path indexPath;
        std::vector<MapDescription> maps;

        std::filesystem::path thumbnail() const;
    };

    //Chaptets
    struct Chaptets: public std::vector<Chapter>
    {
        static Chaptets load(const std::filesystem::path& chaptersPath); 
        static void save(const Chaptets& chapters, const std::filesystem::path& chaptersPath);
    };

}