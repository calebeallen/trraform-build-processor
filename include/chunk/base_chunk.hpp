#pragma once

#include <string>

#include "chunk/d_chunk.hpp"

class BaseChunk : public DChunk {

public:
    BaseChunk() = default;
    BaseChunk(std::string, std::vector<std::uint64_t>, std::vector<UpdateFlags>);

    void prep() override;
    std::optional<std::string> update() override;

};