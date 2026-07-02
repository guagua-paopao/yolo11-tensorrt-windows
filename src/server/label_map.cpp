#include "server/label_map.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace yolo11_server {

    namespace {
        std::string trim(const std::string& text) {
            auto begin = text.begin();
            while (begin != text.end() && std::isspace(static_cast<unsigned char>(*begin))) {
                ++begin;
            }

            auto end = text.end();
            do {
                if (end == begin) {
                    break;
                }
                --end;
            } while (std::isspace(static_cast<unsigned char>(*end)));

            if (begin == text.end()) {
                return {};
            }
            return std::string(begin, end + 1);
        }
    }  // namespace

    bool LabelMap::loadFromFile(const std::string& labels_path, std::string& error) {
        labels_.clear();
        source_path_.clear();
        error.clear();

        if (labels_path.empty()) {
            error = "model.labels_path is empty";
            return false;
        }

        std::ifstream file(labels_path);
        if (!file.is_open()) {
            error = "failed to open labels file: " + labels_path;
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            line = trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            labels_.push_back(line);
        }

        if (labels_.empty()) {
            error = "labels file is empty: " + labels_path;
            return false;
        }

        source_path_ = labels_path;
        return true;
    }

    std::string LabelMap::className(int class_id) const {
        if (class_id >= 0 && class_id < static_cast<int>(labels_.size())) {
            return labels_[static_cast<size_t>(class_id)];
        }
        return "class_" + std::to_string(class_id);
    }

    bool LabelMap::empty() const {
        return labels_.empty();
    }

    size_t LabelMap::size() const {
        return labels_.size();
    }

    std::string LabelMap::sourcePath() const {
        return source_path_;
    }

}  // namespace yolo11_server
