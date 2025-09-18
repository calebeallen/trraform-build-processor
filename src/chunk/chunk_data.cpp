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

int ChunkData::getMappedBwd(const int layer, const int locId){

    if(layer == 0)
        return 0;
    
    if(layer == 1){
        static const std::array<int, VARS::L1_SIZE> map = []() {
            std::array<int, VARS::L1_SIZE> map;
            std::ifstream file("static/cmap_l1.dat", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open cmap_l1.dat");

            int buf[2];
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
                map[buf[1]] = buf[0];
            }

            return map;
        }();
        return map[locId];
    }

    if(layer == 2){
        static const std::array<int, VARS::L2_SIZE> map = []() {
            std::array<int, VARS::L2_SIZE> map;
            std::ifstream file("static/cmap_l2.dat", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open cmap_l2.dat");

            int buf[2];
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
                map[buf[1]] = buf[0];
            }

            return map;
        }();
        return map[locId];
    }

    throw std::runtime_error("Invalid layer");

}

// map from parent id -> child ids
const std::vector<int>& ChunkData::getMappedFwd(const int layer, const int locId){

    if(layer == 0){
        static const std::vector<int> entry = []() {
            std::vector<int> map;
            for(int i = 0; i < VARS::L0_SIZE; ++i)
                map.push_back(i);
            return map;
        }();
        return entry;
    }

    if(layer == 1){
        static const std::array<std::vector<int>, VARS::L0_SIZE> map = []() {
            std::array<std::vector<int>, VARS::L0_SIZE> map;

            std::ifstream file("static/cmap_l1.dat", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open cmap_l1.dat");

            int buf[2];
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
                map[buf[0]].push_back(buf[1]);
            }

            return map;
        }();
        return map[locId];
    }
    
    if(layer == 2){
        static const std::array<std::vector<int>, VARS::L1_SIZE> map = []() {
            std::array<std::vector<int>, VARS::L1_SIZE> map;

            std::ifstream file("static/cmap_l2.dat", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open cmap_l2.dat");

            int buf[2];
            int plotId = 1;
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
                map[buf[0]].push_back(plotId);
                plotId++;
            }

            return map;
        }();
        return map[locId];
    }
        
    throw std::runtime_error("Invalid layer");

}

std::string ChunkData::makeChunkIdStr(const int idl, const int idr, const bool layer){

    return fmt::format("{}{:x}_{:x}", layer ? "l":"", idl, idr);

}

const std::tuple<int,int> ChunkData::parseChunkIdStr(const std::string& id){

    std::istringstream ss(id);
    std::string idlHex, idrHex;
    getline(ss, idlHex, '_');
    getline(ss, idrHex, '_');

    // for low res chunks remove layer prefix
    if(idlHex[0] == 'l')
        idlHex.erase(0,1);
       
    return std::tuple<int,int>(stoi(idlHex, nullptr, 16), stoi(idrHex, nullptr, 16));

}

ChunkData::ChunkData(std::string chunkId, std::vector<std::uint64_t> needsUpdate, std::shared_ptr<CFAsyncClient> cfCli) {
    _chunkId = chunkId;
    _needsUpdate = std::move(needsUpdate);
    _cfCli = cfCli;
};

asio::awaitable<void> ChunkData::downloadParts() {

    auto obj = co_await _cfCli->getR2Object(VARS::CF_CHUNKS_BUCKET, _chunkId + ".dat");
    
    if (obj.err) {
        std::cout << "ChunkId: " << _chunkId << std::endl;
        // if (obj.errType != Aws::S3::S3Errors::NO_SUCH_KEY)
        //     throw std::runtime_error(obj.errMsg);
        co_return;
    }
    
    // decode into parts
    size_t i = 0, n = obj.body.size();
    while (i < n) {
        // read id (64 bit int little endian)
        std::uint64_t id;
        std::memcpy(&id, &obj.body[i], sizeof(std::uint64_t));
        i += 8;

        // read part len metadata (32 bit int little endian)
        std::uint32_t partLen;
        std::memcpy(&partLen, &obj.body[i], sizeof(std::uint32_t));
        i += 4;

        std::vector<std::uint8_t> part(obj.body.begin() + i, obj.body.begin() + i + partLen);
        i += partLen;

        _parts.emplace(id, std::move(part));
    }

}

asio::awaitable<void> ChunkData::uploadParts(){

    // encode parts
    size_t size = 0;
    for(auto& [key, part] : _parts)
        size += 12 + part.size();

    std::vector<uint8_t> data(size);
    size_t i = 0;
    for(auto& [id, part] : _parts){

        // set id
        std::memcpy(&data[i], &id, sizeof(uint64_t));
        i += 8;

        // set part len metadata
        uint32_t partLen = part.size();
        std::memcpy(&data[i], &partLen, sizeof(uint32_t));
        i += 4;

        // set part
        std::copy(part.begin(), part.end(), data.begin() + i);
        i += partLen;

    }

    auto out = co_await _cfCli->putR2Object(VARS::CF_CHUNKS_BUCKET, _chunkId + ".dat", "application/octet-stream", data);
    if (out.err)
        throw std::runtime_error(out.errMsg);

}
