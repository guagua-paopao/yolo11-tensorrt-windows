#pragma once

#include <string>
#include <vector>

namespace yolo11_server {

    class LabelMap {
    public:
        bool loadFromFile(const std::string& labels_path, std::string& error);
        std::string className(int class_id) const;
        bool empty() const;
        size_t size() const;
        std::string sourcePath() const;

    private:
        std::vector<std::string> labels_;
        std::string source_path_;
    };

}  // namespace yolo11_server
