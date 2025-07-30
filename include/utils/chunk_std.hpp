#pragma once
#include "chunk.hpp"
#include <string>

class PlotChunk: Chunk{

private:


public:

    PlotChunk(const std::string);

    void load() override;

};