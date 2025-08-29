#pragma once

#include <string>

#include "chunk/d_chunk.hpp"

class BaseChunk : public DChunk {

protected:
    void savePointCloud();

public:
    BaseChunk(const std::string&);

    std::optional<std::string> update() override;

};