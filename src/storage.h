#pragma once
#include <string>
#include <map>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include <shared_mutex>
#include <memory>
#include <vector>

class Storage {
    public:
        Storage(const std::vector<std::string> &files);
        bool getResource(const std::string &filePath, std::string  &outStr);
        void updateResource();
    private:
        std::string readFile(const std::string &filePath);
        std::string processFilePath(const std::string &filePath);
        void addFile(const std::string filePath);
    private:
        std::string baseDir;
        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, std::string>  resources;
        std::vector<std::string> files;
};