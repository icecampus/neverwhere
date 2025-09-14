#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

//Preset
struct Preset 
{
    std::string asset;
    float scale = 1.0f;
    std::string name;
};


//PresetsPack
struct PresetsPack : public std::vector<Preset> 
{
    void save(const fs::path& folder_path);
    void load(const fs::path& folder_path);
};