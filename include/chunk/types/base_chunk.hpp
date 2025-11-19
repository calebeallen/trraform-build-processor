#pragma once

#include <string>
#include <cstdint>

#include <boost/asio/awaitable.hpp>

#include "chunk/types/d_chunk.hpp"
#include "chunk/types/l_chunk.hpp"
#include "utils/plot.hpp"

class BaseChunk : public DChunk, public LChunk {

public:
    BaseChunk(
        std::string chunkId, 
        std::vector<std::string> needsUpdate, 
        std::vector<Plot::UpdateFlags> updateFlags
    ) : ChunkData(std::move(chunkId), std::move(needsUpdate)),
    DChunk(std::move(updateFlags)), 
    LChunk() {}
    
    asio::awaitable<void> prep(const std::shared_ptr<CFAsyncClient> cfCli) override { co_await DChunk::prep(cfCli); };
    void process() override { DChunk::process(); };
    boost::asio::awaitable<std::optional<std::string>> update(const std::shared_ptr<CFAsyncClient> cfCli) override;

};