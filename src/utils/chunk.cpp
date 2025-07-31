#include <array>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <algorithm>

#include <opencv2/core.hpp>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h> 

#include "constants.hpp"
#include "chunk.hpp"
#include "cf_util.hpp"


// basic function for loading, uploading, encoding, decoding lowres chunks
// stores point clouds for each child chunk
// stores id
// stores layer
// constructor calls load
// load
//      use chunk map (depending on layer) to get child ids for chunk id
//      load all of the point clouds for child chunk ids from fs in memory
// update
//      run kmeans on point clouds, saves box geometry data to r2 bucket
// save
//      samples points and saves them to fs under chunk id (unless layer 0)

// map from child id -> parent id
const int Chunk::getMappedBwd(const int layer, const int locId){

    if(layer == 0)
        return 0;
    
    if(layer == 1){
        static const std::array<int, L1_SIZE> map = []() {
            std::array<int, L1_SIZE> map;
            std::ifstream file("chunk_maps/l1.bin", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open l1.bin");

            int buf[2];
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
                map[buf[1]] = buf[0];
            }

            return map;
        }();
        return map[locId];
    }

    if(layer == 2){
        static const std::array<int, L2_SIZE> map = []() {
            std::array<int, L2_SIZE> map;
            std::ifstream file("chunk_maps/l2.bin", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open l1.bin");

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
const std::vector<int>& Chunk::getMappedFwd(const int layer, const int locId){

    if(layer == 0){
        static const std::vector<int> entry = []() {
            std::vector<int> map;
            for(int i = 0; i < L0_SIZE; ++i)
                map.push_back(i);
            return map;
        }();
        return entry;
    }

    if(layer == 1){
        static const std::array<std::vector<int>, L0_SIZE> map = []() {
            std::array<std::vector<int>, L0_SIZE> map;

            std::ifstream file("chunk_maps/l1.bin", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open l1.bin");

            int buf[2];
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
                map[buf[0]].push_back(buf[1]);
            }

            return map;
        }();
        return map[locId];
    }
    
    if(layer == 2){
        static const std::array<std::vector<int>, L1_SIZE> map = []() {
            std::array<std::vector<int>, L1_SIZE> map;

            std::ifstream file("chunk_maps/l2.bin", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open l2.bin");

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

const std::string Chunk::chunkId(const int layer, const int locId){

    std::ostringstream ss;
    ss << "l" << layer << "_" << locId;
    return ss.str();

}

Chunk::Chunk(const std::string id) {

    // split id into parts
    // format l[layer]_[locId]
    std::stringstream ss(id);
    std::string layerStr, locIdStr;
    getline(ss, layerStr, '_');
    getline(ss, locIdStr, '_');

    layerStr.erase(0,1); // remove l
    layer = stoi(layerStr);
    locId = stoi(locIdStr);
    
    load();

}

void Chunk::downloadParts(){

    // get chunk from cf
    Aws::S3::Model::GetObjectRequest req;
    req.SetBucket(VARS::CF_CHUNKS_BUCKET);
    req.SetKey(chunkId(layer, locId));

    auto res = r2Cli->GetObject(req);
    if (!res.IsSuccess()) 
        return;

    auto& body = res.GetResult().GetBody();
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(body)),
        std::istreambuf_iterator<char>()
    );

    // decode into parts
    size_t i = 0, n = data.size();
    while (i < n) {
        if (i + 4 > n)
            throw std::runtime_error("Not enough bytes to read key");

        // read key (32 bit int little endian)
        uint32_t key =
            static_cast<uint32_t>(data[i]) |
            (static_cast<uint32_t>(data[i + 1]) << 8) |
            (static_cast<uint32_t>(data[i + 2]) << 16) |
            (static_cast<uint32_t>(data[i + 3]) << 24);
        i += 4;

        // read part len metadata (32 bit int little endian)
        uint32_t partLen =
            static_cast<uint32_t>(data[i]) |
            (static_cast<uint32_t>(data[i + 1]) << 8) |
            (static_cast<uint32_t>(data[i + 2]) << 16) |
            (static_cast<uint32_t>(data[i + 3]) << 24);
        i += 4;

        if (i + partLen > n)
            throw std::runtime_error("Not enough bytes to read value");

        std::vector<uint8_t> part(data.begin() + i, data.begin() + i + partLen);
        i += partLen;

        parts[key] = std::move(part);
    }

}


void Chunk::uploadParts(){

    // encode parts
    size_t dataSize = 0;
    for(auto& [key, part] : parts)
        dataSize += 8 + part.size();

    std::vector<uint8_t> data(dataSize);
    size_t i = 0;
    for(auto& [key, part] : parts){

        // set key
        data[i] = static_cast<uint8_t>(key & 0xFF);
        data[i+1] = static_cast<uint8_t>((key >> 8) & 0xFF);
        data[i+2] = static_cast<uint8_t>((key >> 16) & 0xFF);
        data[i+3] = static_cast<uint8_t>((key >> 24) & 0xFF);
        i += 4;

        // set part len metadata
        size_t partLen = part.size();
        data[i] = static_cast<uint8_t>(partLen & 0xFF);
        data[i+1] = static_cast<uint8_t>((partLen >> 8) & 0xFF);
        data[i+2] = static_cast<uint8_t>((partLen >> 16) & 0xFF);
        data[i+3] = static_cast<uint8_t>((partLen >> 24) & 0xFF);
        i += 4;

        // set part
        std::copy(part.begin(), part.end(), data.begin() + i);
        i += partLen;

    }
    
    // upload to cf
    auto stream = Aws::MakeShared<Aws::StringStream>("PutObjectInputStream");
    stream->write(reinterpret_cast<const char*>(data.data()), data.size());

    Aws::S3::Model::PutObjectRequest req;
    req.SetBucket(VARS::CF_CHUNKS_BUCKET);
    req.SetKey(chunkId(layer, locId));
    req.SetBody(stream);
    req.SetContentLength(data.size());
    req.SetContentType("application/octet-stream"); 

    auto res = r2Cli->PutObject(req);
    if (!res.IsSuccess())
        throw std::runtime_error("R2 PutObject failed: " + res.GetError().GetMessage());

}


void Chunk::load(){

    // get child ids of vector
    const std::vector<int>& childLocIds = getMappedFwd(layer, locId);

    // load point cloud for each child from fs
    for(int chli: childLocIds){
        const std::string fname = "/point_clouds/" + chunkId(layer + 1, chli) + ".bin";
        std::ifstream file(fname, std::ios::binary);
        if (!file.is_open()) //files may not exist
            continue;

        int rows;
        file.read(reinterpret_cast<char*>(&rows), sizeof(int));

        cv::Mat pointCloud(rows, 6, CV_32FC1);
        size_t size = pointCloud.total() * pointCloud.elemSize();
        file.read(reinterpret_cast<char*>(pointCloud.data), size);
        file.close();

        childPointClouds[chli] = std::move(pointCloud);
    }

}

void Chunk::save(){

    std::vector<cv::Mat> samples;
    cv::RNG rng;

    // sample points from children
    for(const auto& [_, mat] : childPointClouds){
        int n = mat.rows;
        int nf = (float)(mat.rows);

        int sampleCount = std::max(1, (int)(n * PC_SAMPLE_PERC));

        std::vector<int> idxs(n);
        for (int i = 0; i < n; ++i)
            idxs[i] = i;

        std::shuffle(idxs.begin(), idxs.end(), rng);

        for (int i = 0; i < sampleCount; ++i)
            samples.push_back(mat.row(idxs[i]));
    }

    cv::Mat pointCloud;
    cv::vconcat(samples, pointCloud);
 
    // write to file
    const std::string fname = "/point_clouds/" + chunkId(layer, locId) + ".bin";
    std::ofstream file(fname, std::ios::binary);  
    if (!file.is_open()) {
        std::cerr << "Error opening file for writing: " << fname << std::endl;
        return;
    }

    int rows = pointCloud.rows;
    file.write(reinterpret_cast<const char*>(&rows), sizeof(int));

    size_t dataSize = pointCloud.total() * pointCloud.elemSize();
    file.write(reinterpret_cast<const char*>(pointCloud.data), dataSize);

}
