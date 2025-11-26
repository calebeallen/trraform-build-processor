#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>

#include "utils_/chunk_id.hpp"
#include "async/cf_async_client.hpp"

namespace asio = boost::asio;

class Chunk {

public:
    Chunk(
        const ChunkId& chunk_id,
        std::vector<uint64_t>&& needs_update
    );

    virtual asio::awaitable<void> prep(CFAsyncClient& cf_cli);
    virtual void process() = 0;
    virtual asio::awaitable<void> update(CFAsyncClient& cf_cli);

private:
    std::vector<uint8_t> serialized_chunk_;
    std::vector<uint64_t> needs_update_;
    std::vector<std::vector<uint8_t>> serialized_updates_;
    ChunkId chunk_id_;

};