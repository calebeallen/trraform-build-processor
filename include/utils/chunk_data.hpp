#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>

class ChunkData {

protected:
    std::unordered_map<int,std::vector<uint8_t>> parts; 

public:
    std::vector<std::string> needsUpdate;
    std::string chunkId;

    static const std::vector<int>& getMappedFwd(const int, const int);
    static const int getMappedBwd(const int, const int);
    static std::string makeChunkIdStr(const int, const int, const bool);
    static const std::tuple<int,int> parseChunkIdStr(const std::string&);

    void loadParts();
    void uploadParts();
    virtual void process();
    virtual void update();

};