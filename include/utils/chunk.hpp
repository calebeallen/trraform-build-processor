#pragma once
#include <vector>
#include <string>
#include <opencv4/opencv2/core/mat.hpp>

inline constexpr int L1_MAP_SIZE = 87;
inline constexpr int L2_MAP_SIZE = 7571;

class Chunk{

private:
    std::vector<cv::Mat> parts;

    int layer;
    int locId;

public:

    Chunk(const std::string);

    static const int getMapped(const int, const int);

    void load();

};