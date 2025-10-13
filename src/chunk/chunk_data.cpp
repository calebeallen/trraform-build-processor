#include <fstream>
#include <sstream>
#include <tuple>
#include <array>
#include <stdexcept>
#include <cstdint>
#include <bit>
#include <cstring>

#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h> 
#include <opencv2/core.hpp>
#include <boost/asio/awaitable.hpp>
#include <fmt/format.h>

#include "config/config.hpp"
#include "chunk/chunk_data.hpp"
#include "chunk/chunk.hpp"
#include "async/cf_async_client.hpp"

ChunkData::ChunkData(
    std::string chunkId, 
    std::vector<std::string> needsUpdate
): _chunkId(std::move(chunkId)) {
    const auto [idl, idr] = Chunk::parseIdStr(_chunkId);
    _idl = idl;
    _idr = idr;

    _needsUpdate.reserve(needsUpdate.size());
    for (const auto& s : needsUpdate)
        _needsUpdate.push_back(stoll(s, nullptr, 16));
}

asio::awaitable<void> ChunkData::downloadParts(const std::shared_ptr<const CFAsyncClient> cfCli, bool keepAll) {

    auto obj = co_await cfCli->getR2Object(VARS::CF_CHUNKS_BUCKET, _chunkId);
    if (obj.err) {
        if (obj.errType != Aws::S3::S3Errors::NO_SUCH_KEY)
            throw std::runtime_error(obj.errMsg);
        co_return;
    }
    
    std::unordered_set<uint64_t> nuSet(_needsUpdate.begin(), _needsUpdate.end());
    
    size_t i = 2; // first 2 bytes reserved
    while (i < obj.body.size()) {
        // read id (64 bit int little endian)
        uint64_t id;
        std::memcpy(&id, obj.body.data() + i, sizeof(std::uint64_t));
        i += 8;

        // read part len metadata (32 bit int little endian)
        uint32_t partLen;
        std::memcpy(&partLen, obj.body.data() + i, sizeof(uint32_t));
        i += 4;

        // if keep all false, only keep items that do not need update
        if (keepAll || (!keepAll && !nuSet.contains(id))) {
            std::vector<uint8_t> part(partLen);
            std::memcpy(part.data(), obj.body.data() + i, partLen);
            _parts.emplace(id, std::move(part));
        }
        i += partLen;
    }
}

asio::awaitable<void> ChunkData::uploadParts(const std::shared_ptr<const CFAsyncClient> cfCli) const {
    // encode parts
    size_t size = 2; // reserve first 2 bytes (version)
    for(auto& [key, part] : _parts)
        size += 12 + part.size();

    std::vector<uint8_t> data(size);
    size_t i = 2; // first two bytes reserved
    for(auto& [id, part] : _parts){
        // set id
        std::memcpy(data.data() + i, &id, sizeof(uint64_t));
        i += 8;

        // set part len metadata
        uint32_t partLen = part.size();
        std::memcpy(data.data() + i, &partLen, sizeof(uint32_t));
        i += 4;

        // set part
        std::memcpy(data.data() + i, part.data(), part.size());
        i += partLen;
    }

    auto out = co_await cfCli->putR2Object(
        VARS::CF_CHUNKS_BUCKET, 
        _chunkId, 
        "application/octet-stream", 
        std::move(data) 
    );

    if (out.err)
        throw std::runtime_error(out.errMsg);
}
