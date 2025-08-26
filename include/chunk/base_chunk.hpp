#pragma once

#include "chunk/d_chunk.hpp"

class BaseChunk : public DChunk {

protected:
    void savePointCloud();

public:
    void update() override;

};