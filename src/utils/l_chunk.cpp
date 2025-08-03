#include <fstream>
#include <algorithm>

#include "constants.hpp"
#include "l_chunk.hpp"


void LChunk::loadPointClouds(){

    // get child ids of vector
    auto& idParts = parseChunkIdStr(chunkId);
    auto layer = std::get<0>(idParts);
    auto locId = std::get<1>(idParts);
    const std::vector<int>& childLocIds = getMappedFwd(layer, locId);

    // load point clouds for each child from fs
    for(int chli: childLocIds){
        const std::string fname = "/point_clouds/" + chunkId + ".bin";
        std::ifstream file(fname, std::ios::binary);
        if (!file.is_open()) //files may not exist
            continue;

        int rows;
        file.read(reinterpret_cast<char*>(&rows), sizeof(int));

        cv::Mat pointCloud(rows, 6, CV_32FC1);
        size_t size = pointCloud.total() * pointCloud.elemSize();
        file.read(reinterpret_cast<char*>(pointCloud.data), size);
        file.close();

        pointClouds[chli] = std::move(pointCloud);
    }

}

void LChunk::savePointCloud(){

    std::vector<cv::Mat> samples;
    cv::RNG rng;

    // sample points from children
    for(const auto& [_, mat] : pointClouds){
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
    const std::string fname = "/point_clouds/" + chunkId + ".dat";
    std::ofstream file(fname, std::ios::binary);  
    if (!file.is_open()) {
        
        return;
    }

    int rows = pointCloud.rows;
    file.write(reinterpret_cast<const char*>(&rows), sizeof(int));

    size_t dataSize = pointCloud.total() * pointCloud.elemSize();
    file.write(reinterpret_cast<const char*>(pointCloud.data), dataSize);

}