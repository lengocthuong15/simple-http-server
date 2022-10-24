#include <string>
#include <map>
#include <mutex>
#include <iostream>

class Storage {
    public:
        Storage();
        bool getResource(const std::string &filePath, std::string &outStr);

    private:
        bool readFile(const std::string &filePath, std::string &outStr);
        std::string processFilePath(const std::string &filePath);
    private:
        std::string baseDir;
        mutable std::mutex mutex_;
        std::map<std::string, std::string>  resources;

};