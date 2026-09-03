#include "pch.h"

#include "FileDialog.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

#include <imgui.h>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool matchesFilter(const fs::path& p, const std::string& filter) {
    if (filter.empty()) return true;
    return lower(p.extension().string()) == lower(filter);
}

void relist(FileDialogState& st) {
    st.dirs.clear();
    st.files.clear();
    st.error.clear();
    std::error_code ec;
    if (st.dir.empty() || !fs::is_directory(st.dir, ec)) {
        st.error = "not a directory: " + st.dir.string();
        return;
    }
    for (fs::directory_iterator it(st.dir, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& e = *it;
        const std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code ec2;
        if (e.is_directory(ec2)) {
            st.dirs.push_back(e);
        } else if (e.is_regular_file(ec2) && matchesFilter(e.path(), st.filter)) {
            st.files.push_back(e);
        }
    }
    if (ec) st.error = ec.message();
    auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return lower(a.path().filename().string()) < lower(b.path().filename().string());
    };
    std::sort(st.dirs.begin(), st.dirs.end(), byName);
    std::sort(st.files.begin(), st.files.end(), byName);
    std::snprintf(st.dirBuf, sizeof(st.dirBuf), "%s", st.dir.string().c_str());
    st.dirty = false;
}

void gotoDir(FileDialogState& st, const fs::path& dir) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(dir, ec);
    st.dir = ec ? dir : canon;
    st.selectedName.clear();
    st.dirty = true;
}

}  // namespace

fs::path findPggCorpusDir(const fs::path& from) {
    std::error_code ec;
    fs::path cur = fs::weakly_canonical(from, ec);
    if (ec) cur = from;
    for (int depth = 0; depth < 12 && !cur.empty(); ++depth) {
        const fs::path candidate = cur / "src" / "tests" / "pgg" / "corpus";
        if (fs::is_directory(candidate, ec)) return candidate;
        const fs::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return {};
}

void fileDialogOpen(FileDialogState& st, const fs::path& startDir) {
    std::error_code ec;
    fs::path dir = startDir;
    if (dir.empty()) dir = fs::current_path(ec);
    if (fs::is_regular_file(dir, ec)) dir = dir.parent_path();
    if (!fs::is_directory(dir, ec)) dir = fs::current_path(ec);
    gotoDir(st, dir);

    st.bookmarks.clear();
    const fs::path corpus = findPggCorpusDir(fs::current_path(ec));
    if (!corpus.empty()) st.bookmarks.push_back(corpus);
    const fs::path cwd = fs::current_path(ec);
    if (!cwd.empty() && cwd != corpus) st.bookmarks.push_back(cwd);
    st.open = true;
}

std::optional<fs::path> fileDialogDraw(FileDialogState& st, const char* title) {
    if (!st.open) return std::nullopt;
    if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_Appearing);

    std::optional<fs::path> result;
    bool keepOpen = true;
    if (!ImGui::BeginPopupModal(title, &keepOpen, ImGuiWindowFlags_NoSavedSettings)) {
        st.open = false;
        return std::nullopt;
    }
    if (st.dirty) relist(st);

    // Path row: Up + editable directory + Go.
    if (ImGui::Button("Up")) {
        const fs::path parent = st.dir.parent_path();
        if (!parent.empty() && parent != st.dir) gotoDir(st, parent);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-60.0f);
    const bool pathEntered =
        ImGui::InputText("##dir", st.dirBuf, sizeof(st.dirBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Go") || pathEntered) {
        std::error_code ec;
        const fs::path typed(st.dirBuf);
        if (fs::is_regular_file(typed, ec)) {
            result = typed;
        } else {
            gotoDir(st, typed);
        }
    }

    // Bookmarks.
    if (!st.bookmarks.empty()) {
        ImGui::TextDisabled("Go to:");
        for (size_t i = 0; i < st.bookmarks.size(); ++i) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i));
            const std::string label = st.bookmarks[i].filename().string();
            if (ImGui::SmallButton(label.empty() ? "/" : label.c_str())) gotoDir(st, st.bookmarks[i]);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", st.bookmarks[i].string().c_str());
            ImGui::PopID();
        }
    }

    if (!st.error.empty()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "%s", st.error.c_str());

    // Listing.
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("##listing", ImVec2(0.0f, -footer), true);
    for (const fs::directory_entry& d : st.dirs) {
        const std::string label = "[" + d.path().filename().string() + "]";
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                gotoDir(st, d.path());
                break;  // listing invalidated
            }
        }
    }
    for (const fs::directory_entry& f : st.files) {
        const std::string name = f.path().filename().string();
        const bool selected = (name == st.selectedName);
        if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            st.selectedName = name;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) result = f.path();
        }
    }
    if (st.dirs.empty() && st.files.empty() && st.error.empty())
        ImGui::TextDisabled("(no %s files here)", st.filter.empty() ? "" : st.filter.c_str());
    ImGui::EndChild();

    // Footer: filter toggle + Open / Cancel.
    bool showAll = st.filter.empty();
    if (ImGui::Checkbox("all files", &showAll)) {
        st.filter = showAll ? "" : ".pgg";
        st.dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", st.selectedName.empty() ? "select a file" : st.selectedName.c_str());

    // Right-align Open/Cancel on the same row.
    const float btnW = 90.0f;
    const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(rowRight - btnW * 2.0f - ImGui::GetStyle().ItemSpacing.x);
    const bool canOpen = !st.selectedName.empty();
    if (!canOpen) ImGui::BeginDisabled();
    if (ImGui::Button("Open", ImVec2(btnW, 0.0f)) ||
        (canOpen && ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().WantTextInput))
        result = st.dir / st.selectedName;
    if (!canOpen) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(btnW, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) keepOpen = false;

    if (result || !keepOpen) {
        st.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return result;
}
