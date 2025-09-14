#include "presets.h"



void to_json(json& j, const Preset& p) 
{
    j = json{
        {"asset", p.asset},
        {"scale", p.scale}
    };
}

void from_json(const json& j, Preset& p) 
{
    j.at("asset").get_to(p.asset);
    j.at("scale").get_to(p.scale);
}

//PresetsPack
void PresetsPack::save(const fs::path& folder_path) 
{
    fs::create_directories(folder_path);

    for (const auto& preset : *this) 
    {
        fs::path file_path = folder_path / (preset.name + ".json");

        std::ofstream file(file_path);
        json j = preset;
        file << j.dump(4);
    }
}

void PresetsPack::load(const fs::path& folder_path) 
{
    clear();

    if (!fs::exists(folder_path))
    {
        return;
    }

    for (const auto& entry : fs::directory_iterator(folder_path)) 
    {
        if (entry.path().extension() == ".json") {
            std::string name = entry.path().stem().string();

            std::ifstream file(entry.path());
            json j;
            file >> j;

            Preset preset;
            from_json(j, preset);
            preset.name = name;

            push_back(preset);
        }
    }
}
