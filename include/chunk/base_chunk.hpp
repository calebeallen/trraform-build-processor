#pragma once

#include <string>
#include <cstdint>

#include <boost/asio/awaitable.hpp>

#include "chunk/d_chunk.hpp"

class BaseChunk : public DChunk {

public:
    BaseChunk(std::string, std::vector<std::string>, std::vector<UpdateFlags>, std::shared_ptr<CFAsyncClient>);

    boost::asio::awaitable<std::optional<std::string>> update() override;

};