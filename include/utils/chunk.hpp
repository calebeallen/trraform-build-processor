#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <tuple>

#include <opencv2/core/mat.hpp>

inline constexpr int L0_SIZE = 87;
inline constexpr int L1_SIZE = 7571;
inline constexpr int L2_SIZE = 34998;
inline constexpr float PC_SAMPLE_PERC = 0.1F;

class Chunk{

protected:
    std::vector<int> needsUpdate;
    std::unordered_map<int,cv::Mat> pointClouds;
    std::unordered_map<int,std::vector<uint8_t>> parts; 
    std::string chunkId;

    void downloadParts();
    void uploadParts();

public:
    static const std::vector<int>& getMappedFwd(const int, const int);
    static const int getMappedBwd(const int, const int);
    static std::string makeChunkIdStr(const int, const int, const bool);
    static const std::tuple<int,int> parseChunkIdStr(const std::string&);

    Chunk(const std::string&);

    virtual void load();
    void save();

};