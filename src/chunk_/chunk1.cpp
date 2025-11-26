#include <vector>
#include <string>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>

#include "chunk_/chunk1.hpp"
#include "config/config.hpp"

Chunk::Chunk(
    const ChunkId& chunk_id,
    std::vector<uint64_t>&& needs_update
) : chunk_id_(chunk_id), needs_update_(std::move(needs_update)) {};

asio::awaitable<void> Chunk::prep(CFAsyncClient& cf_cli) {
    auto res = co_await cf_cli.getR2Object(VARS::CF_CHUNKS_BUCKET, chunk_id_.get_string(), true);
    if (res.err) {
        if (res.errType != Aws::S3::S3Errors::NO_SUCH_KEY)
            throw std::runtime_error(res.errMsg);
        co_return;
    }

    serialized_chunk_ = std::move(res.body);
    co_return;
};

asio::awaitable<void> Chunk::update(CFAsyncClient& cf_cli) {
    auto res = co_await cf_cli.putR2Object(
        VARS::CF_CHUNKS_BUCKET, 
        chunk_id_.get_string(), 
        "application/octet-stream", 
        std::move(serialized_chunk_),
        true
    );

    if (res.err)
        throw std::runtime_error(res.errMsg);
};