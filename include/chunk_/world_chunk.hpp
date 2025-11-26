#pragma once

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>

#include "chunk_/chunk1.hpp"
#include "async/async_semaphore.hpp"

namespace asio = boost::asio;

class WorldChunk : public Chunk {

public:
    virtual asio::awaitable<void> prep(CFAsyncClient& cf_cli) override;
    virtual void process() override;
    virtual asio::awaitable<void> update(CFAsyncClient& cf_cli) override;

};