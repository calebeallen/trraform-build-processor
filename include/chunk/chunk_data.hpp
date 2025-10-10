#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <optional>

#include <boost/asio/awaitable.hpp>

#include "utils/cf_async_client.hpp"

namespace asio = boost::asio;

class ChunkData {

protected:
    std::unordered_map<uint64_t, std::vector<uint8_t>> _parts;
    std::vector<std::string> _needsUpdateStr;
    std::vector<uint64_t> _needsUpdate;
    std::string _chunkId;

    asio::awaitable<void> downloadParts(const std::shared_ptr<const CFAsyncClient> cfCli, bool keepAll = false);
    asio::awaitable<void> uploadParts(const std::shared_ptr<const CFAsyncClient> cfCli) const;

public:
    ChunkData() = default;
    ChunkData(std::string chunkId, std::vector<std::string> needsUpdate);
    virtual ~ChunkData() = default;

    virtual asio::awaitable<void> prep(const std::shared_ptr<const CFAsyncClient> cfCli) = 0;
    virtual void process() = 0;
    virtual asio::awaitable<std::optional<std::string>> update(const std::shared_ptr<const CFAsyncClient> cfCli) = 0;

};