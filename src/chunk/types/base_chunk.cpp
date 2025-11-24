#include <vector>
#include <span>
#include <utility>
#include <tuple>
#include <fstream>
#include <random>
#include <string>

#include <opencv2/core.hpp>
#include <cpr/cpr.h>
#include <boost/asio/awaitable.hpp>

#include "chunk/types/base_chunk.hpp"
#include "config/config.hpp"
#include "utils/color_lib.hpp"
#include "utils/utils.hpp"
#include "utils/plot.hpp"
#include "chunk/chunk.hpp"

asio::awaitable<std::optional<std::string>> BaseChunk::update(const std::shared_ptr<CFAsyncClient> cfCli) {

    co_await uploadParts(cfCli);
    co_await uploadImages(cfCli);

    co_await downloadPointCloud(cfCli);

    // get point cloud vectors from build data
    std::mt19937 rng{std::random_device{}()};
    for (const auto& id : _needsUpdate) {
        const auto& part = _parts[id];
        const auto buildData = Plot::getBuildPart(part);

        // extract non-empty block position and color indices
        std::vector<std::pair<uint32_t,uint16_t>> build;
        uint32_t posidx = 0;
        uint16_t colidx;
        for (size_t i = 2; i < buildData.size(); ++i) {
            uint16_t val = buildData[i] >> 1;
            if (buildData[i] & 1) {
                colidx = val;
                if (colidx > VARS::PLOT_COUNT)
                    build.emplace_back(posidx, colidx);
                ++posidx;
            } else {
                if (colidx > VARS::PLOT_COUNT)
                    for(int j = 0; j < val; ++j)
                        build.emplace_back(posidx+j, colidx);
                posidx += val;
            }
        }
        
        // a build must have 2 or more blocks to be processed later for low res chunks
        if (build.size() < 2ul)
            continue;
            
        std::shuffle(build.begin(), build.end(), rng);

        const size_t k = std::max(2ul, static_cast<size_t>(std::sqrt(build.size())));
        cv::Mat points(k, 3, CV_32F);    
        std::vector<uint16_t> colidxs(k); 

        cv::Vec3f worldPos = Utils::idxToVec3(Chunk::plotIdToPosIdx(id), VARS::MAIN_BUILD_SIZE);
        worldPos[1] += 1.f;
        const float buildSize = static_cast<float>(buildData[1]);
        const cv::Vec3f centerOffset(0.5f,0.5f,0.5f);

        for (size_t i = 0; i < k; ++i) {
            auto pos = Utils::idxToVec3(build[i].first, buildSize);
            pos += centerOffset;
            pos /= buildSize;
            pos += worldPos;

            std::memcpy(points.ptr<float>(i), pos.val, 3 * sizeof(float));
            colidxs[i] = build[i].second;
        }

        _pointClouds[id] = PointCloud{std::move(points), std::move(colidxs)};
    }
  

    if (!_pointClouds.empty()) {
        co_await uploadPointCloud(cfCli);

        // make next update chunk id
        const auto splitId = Chunk::parseIdStr(_chunkId);
        const auto nextLocId = Chunk::mapBwd(1, splitId.second);
        const auto nextChunkId = Chunk::makeIdStr(1, nextLocId, true);
        co_return nextChunkId;
    }

    co_return std::nullopt;
}
