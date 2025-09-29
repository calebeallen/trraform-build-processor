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
    std::unordered_map<std::string, std::vector<uint8_t>> _parts; 
    std::vector<std::string> _needsUpdate;
    std::string _chunkId;
    std::shared_ptr<CFAsyncClient> _cfCli;

    asio::awaitable<void> downloadParts();
    asio::awaitable<void> uploadParts();

public:
    static const std::vector<int>& getMappedFwd(const int, const int);
    static uint32_t getMappedBwd(const int, const int);
    static std::string makeChunkIdStr(const int, const int, const bool);
    static const std::tuple<int,int> parseChunkIdStr(const std::string&);

    ChunkData(std::string, std::vector<std::string>, std::shared_ptr<CFAsyncClient>);
    virtual ~ChunkData() = default;

    virtual asio::awaitable<void> prep() = 0;
    virtual void process() = 0;
    virtual asio::awaitable<std::optional<std::string>> update() = 0;

};