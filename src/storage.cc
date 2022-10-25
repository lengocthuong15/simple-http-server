#include <storage.h>
#include <fstream>
#include <iostream>

Storage::Storage(const std::vector<std::string> &files): files(files){
    this->baseDir = "/var/run/simplehttp";
    this->updateResource();
};


std::string Storage::processFilePath(const std::string &filePath) {
    return this->baseDir + filePath;
}

void Storage::addFile(const std::string filePath) {
    this->files.push_back(filePath);
    std::string content = this->readFile(filePath);
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        this->resources.emplace(filePath, content);
    }
}

std::string Storage::readFile(const std::string &filePath)
{
    std::string inFile = this->processFilePath(filePath);
    // std::cout << "File path = " << inFile << std::endl;
    std::ifstream in(inFile, std::ios::in | std::ios::binary);
    if (in)
    {
        std::string contents;
        in.seekg(0, std::ios::end);
        contents.resize(in.tellg());
        in.seekg(0, std::ios::beg);
        in.read(&contents[0], contents.size());
        in.close();
        return contents;
    }
    return std::string();
}

void Storage::updateResource() {
    for (const auto filePath : files) {
        std::string content = this->readFile(filePath);
        if (content.length() != 0) {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            this->resources.erase(filePath);
            this->resources.emplace(filePath, content);
            // std::cout << "Add new resource, filepath = " << filePath << std::endl;
        }
    }
}

bool Storage::getResource(const std::string &filePath, std::string  &outStr) {
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = this->resources.find(filePath);
        if (it != this->resources.end()) {
            outStr = it->second;
            return true;
        }
        return false;
    }
    // // Cannot find anything on the cache, read from the file
    // std::cout << "readFile, filepaht = " << filePath << std::endl;
    // std::string content = this->readFile(filePath);
    // {
    //     std::unique_lock<std::shared_mutex> lock(mutex_);
    //     std::shared_ptr<std::string> newContent = std::make_shared<std::string>(content);
    //     this->resources.emplace(filePath, newContent);
    //     std::cout << "Add new resource, filepaht = " << filePath << std::endl;
    //     outStr = *newContent;
    // }
}
