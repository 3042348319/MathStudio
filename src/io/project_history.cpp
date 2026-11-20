// ====================================================================
// project_history.cpp
// ====================================================================

#include "project_history.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {
constexpr std::size_t kMaxEntries = 16;

std::filesystem::path historyPath() {
    return std::filesystem::current_path() / "projects" / "history.txt";
}
}  // namespace

ProjectHistory::ProjectHistory() {
    load();
}

void ProjectHistory::load() {
    entries_.clear();

    std::ifstream file(historyPath());
    if (!file.is_open()) {        return;
    }

    std::string line;
    while (std::getline(file, line) && entries_.size() < kMaxEntries) {
        if (!line.empty()) {
            entries_.push_back(line);
        }
    }
}

bool ProjectHistory::add(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::error_code error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(std::filesystem::u8path(path), error);
    const std::string normalized =
        error ? path : absolute.lexically_normal().u8string();

    entries_.erase(
        std::remove(entries_.begin(), entries_.end(), normalized),
        entries_.end());
    entries_.insert(entries_.begin(), normalized);
    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
    }

    save();
    return true;
}

const std::vector<std::string>& ProjectHistory::entries() const {
    return entries_;
}

void ProjectHistory::save() const {
    std::error_code error;
    std::filesystem::create_directories(
        historyPath().parent_path(), error);

    std::ofstream file(historyPath(), std::ios::trunc);
    if (!file.is_open()) {
        return;
    }

    for (const std::string& entry : entries_) {
        file << entry << '\n';
    }
}
