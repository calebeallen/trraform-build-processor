#pragma once
#include <vector>
#include <string>
#include <unordered_map>

#include <opencv2/core/mat.hpp>

inline constexpr int L0_SIZE = 87;
inline constexpr int L1_SIZE = 7571;
inline constexpr int L2_SIZE = 34998;
inline constexpr float PC_SAMPLE_PERC = 0.1F;

class Chunk{

private:
    std::unordered_map<int,cv::Mat> childPointClouds;

    int layer;
    int locId;

public:
    static const std::vector<int>& getMappedFwd(const int, const int);
    static const int getMappedBwd(const int, const int);
    static const std::string chunkId(const int, const int);

    Chunk(const std::string);

    virtual void load();
    void save();

};