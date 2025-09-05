#pragma once

#include <string>

#include <boost/asio/awaitable.hpp>

#include "chunk/d_chunk.hpp"

class BaseChunk : public DChunk {

public:
    BaseChunk(std::string, std::vector<std::uint64_t>, std::vector<UpdateFlags>, std::shared_ptr<CFAsyncClient>);

    boost::asio::awaitable<void> prep() override;
    boost::asio::awaitable< std::optional<std::string>> update() override;

};