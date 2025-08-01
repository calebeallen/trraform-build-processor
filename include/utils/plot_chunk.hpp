#pragma once
#include "chunk.hpp"
#include <opencv2/core.hpp>
#include <string>
#include <span>

class PlotChunk: Chunk{

private:
    static const std::span<uint8_t> getBuildData(std::vector<uint8_t>&);

public:
    PlotChunk(const std::string&);

    void load() override;
    void updatePlots(std::vector<std::string>&);

};