#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>

class ChunkData {

protected:
    std::unordered_map<uint64_t,std::vector<uint8_t>> _parts; 
    std::vector<std::uint64_t> _needsUpdate;
    std::string _chunkId;

    static const std::vector<int>& getMappedFwd(const int, const int);
    static const int getMappedBwd(const int, const int);
    static std::string makeChunkIdStr(const int, const int, const bool);
    static const std::tuple<int,int> parseChunkIdStr(const std::string&);

    void downloadParts();
    void uploadParts();

public:
    ChunkData(const std::string&);

    virtual void process();
    virtual void update();

};