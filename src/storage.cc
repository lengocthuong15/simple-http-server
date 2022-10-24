#include <storage.h>
#include <fstream>

Storage::Storage(){
    this->baseDir = "/var/run/simplehttp/";
};


std::string Storage::processFilePath(const std::string &filePath) {
    return this->baseDir + filePath;
}

bool Storage::readFile(const std::string &filePath, std::string &outStr)
{
    std::string inFile = this->processFilePath(filePath);
    std::ifstream in(inFile, std::ios::in | std::ios::binary);
    if (in)
    {
        std::string contents;
        in.seekg(0, std::ios::end);
        contents.resize(in.tellg());
        in.seekg(0, std::ios::beg);
        in.read(&contents[0], contents.size());
        in.close();
        outStr = contents;
        return true;
    }
    return false;
}

bool Storage::getResource(const std::string &filePath, std::string &outStr) {
    {
        // Trying to check the resource first
        std::shared_lock lock(mutex_);
        auto it = this->resources.find(filePath);
        if (it != this->resources.end()) {
            outStr = it->second;
            return true;
        }
    }
    // Cannot find anything on the cache, read from the file
    this->readFile(filePath, outStr);
    std::string tempStr = outStr;
    {
        std::unique_lock lock(mutex_);
        this->resources.insert(filePath, tempStr);
    }
}
