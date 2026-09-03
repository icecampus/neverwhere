// Minimal ImGui file browser for opening .pgg files (no OS dialog dependency:
// the viewer runs on sokol_app across win/mac/linux and the project does not
// pull native dialog libraries). Directory listing via std::filesystem.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct FileDialogState {
    bool open = false;             // popup requested / visible
    std::filesystem::path dir;     // directory being browsed
    std::string filter = ".pgg";   // extension shown (case-insensitive); empty = all files
    std::string selectedName;      // highlighted entry (file name only)
    std::string error;             // last listing error, shown inline
    std::vector<std::filesystem::directory_entry> dirs;
    std::vector<std::filesystem::directory_entry> files;
    bool dirty = true;             // relist on next draw
    char dirBuf[1024] = {0};       // editable path field
    std::vector<std::filesystem::path> bookmarks;  // quick-jump targets
};

// Opens the dialog at `startDir` (or its parent if it is a file).
void fileDialogOpen(FileDialogState& st, const std::filesystem::path& startDir);

// Draws the modal when open. Returns the chosen file path once (on Open /
// double-click / Enter); nullopt otherwise. Closes itself on Cancel / Esc.
std::optional<std::filesystem::path> fileDialogDraw(FileDialogState& st, const char* title = "Open .pgg file");

// Walks up from `from` looking for `<ancestor>/src/tests/pgg/corpus`; empty if not found.
std::filesystem::path findPggCorpusDir(const std::filesystem::path& from);
