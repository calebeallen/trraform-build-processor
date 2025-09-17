#include <vector>
#include <span>
#include <utility>
#include <tuple>
#include <fstream>
#include <random>

#include <opencv2/core.hpp>
#include <cpr/cpr.h>
#include <boost/asio/awaitable.hpp>

#include "chunk/base_chunk.hpp"
#include "config/config.hpp"
#include "utils/color_lib.hpp"
#include "utils/utils.hpp"
#include "utils/plot.hpp"
#include "utils/cf_async_client.hpp"


BaseChunk::BaseChunk(
    std::string chunkId, 
    std::vector<std::uint64_t> needsUpdate, 
    std::vector<UpdateFlags> updateFlags,
    std::shared_ptr<CFAsyncClient> cfCli
) : DChunk(chunkId, std::move(needsUpdate), std::move(updateFlags), cfCli) {}

asio::awaitable<std::optional<std::string>> BaseChunk::update() {

    co_await uploadParts();
    co_await uploadImages();

    // sample points for parent chunk to use
    std::mt19937 rng{std::random_device{}()};
    std::vector<cv::Mat> points;

    for (auto& [plotId, part] : _parts) {
        const auto buildData = Plot::getBuildPart(part);
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
            
        size_t sampleCount = std::max(1ul, (size_t)(static_cast<float>(vox.size()) * VARS::PC_SAMPLE_PERC));
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
        co_return std::nullopt;
    }

    int rows = pointCloud.rows;
    file.write(reinterpret_cast<const char*>(&rows), sizeof(int));

    size_t dataSize = pointCloud.total() * pointCloud.elemSize();
    file.write(reinterpret_cast<const char*>(pointCloud.data), dataSize);

    // make next update chunk id
    const auto locId = std::get<0>(parseChunkIdStr(_chunkId));
    const auto nextLocId = getMappedBwd(1, locId);
    const auto nextChunkId = makeChunkIdStr(1, nextLocId, true);

    co_return nextChunkId;

}
