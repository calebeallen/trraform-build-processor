#include <vector>
#include <span>
#include <utility>
#include <tuple>
#include <fstream>

#include <opencv2/core.hpp>

#include "chunk/base_chunk.hpp"
#include "config/config.hpp"
#include "utils/color_lib.hpp"
#include "utils/utils.hpp"

void BaseChunk::savePointCloud(){

    cv::RNG rng;
    std::vector<cv::Mat> points;

    for (auto& [plotId, part] : _parts) {
        const auto buildData = getBuildData(part);
        const int bs = buildData[1];

        std::vector<std::tuple<int,int>> vox;
        int idx = 0, col;
        const size_t bdLen = buildData.size();
        for (size_t i = 2; i < bdLen; ++i) {
            int val = buildData[i] >> 1;
            if (buildData[i] & 1) {
                col = val;
                if (col > VARS::PLOT_COUNT)
                    vox.emplace_back(idx, col);
                ++idx;
            } else {
                if (col > VARS::PLOT_COUNT) {
                    for(int j = 0; j < val; ++j)
                        vox.emplace_back(idx+j, col);
                }
                idx += val;
            }
        }

        if (!vox.size())
            continue;
            
        int sampleCount = std::max(1, (int)(static_cast<float>(vox.size()) * VARS::PC_SAMPLE_PERC));
        std::shuffle(vox.begin(), vox.end(), rng);

        int worldPosIdx = getMappedBwd(2, plotId);
        cv::Mat worldPos(1, 3, CV_32F, Utils::idxToVec3(worldPosIdx, VARS::MAIN_BUILD_SIZE).val);

        for (size_t i = 0; i < sampleCount; ++i) {
            cv::Mat pos(1, 3, CV_32F, Utils::idxToVec3(std::get<0>(vox[i]), bs).val);
            cv::Mat col(*ColorLib::getColor(std::get<1>(vox[i])));
            pos += 0.5f;
            pos /= bs;
            pos += worldPos;
            points.push_back(std::move(pos));
        }
    }

    cv::Mat pointCloud;
    cv::vconcat(points, pointCloud);
 
    // write to file
    const std::string fname = "/point_clouds/" + _chunkId + ".dat";
    std::ofstream file(fname, std::ios::binary);  
    if (!file.is_open()) {
        return;
    }

    int rows = pointCloud.rows;
    file.write(reinterpret_cast<const char*>(&rows), sizeof(int));

    size_t dataSize = pointCloud.total() * pointCloud.elemSize();
    file.write(reinterpret_cast<const char*>(pointCloud.data), dataSize);

}

void BaseChunk::update() {
    uploadParts();
    savePointCloud();
}
