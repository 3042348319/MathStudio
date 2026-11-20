// ====================================================================
// project_history.h —— 最近打开的 .math 工程
//
// 历史记录保存在运行目录的 projects/history.txt 中。
// ====================================================================
#pragma once

#include <string>
#include <vector>

class ProjectHistory {
public:
    ProjectHistory();

    void load();
    bool add(const std::string& path);

    const std::vector<std::string>& entries() const;

private:
    void save() const;

    std::vector<std::string> entries_;
};
