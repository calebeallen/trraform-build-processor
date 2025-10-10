#include <vector>
#include <span>
#include <utility>
#include <tuple>
#include <fstream>
#include <random>

#include <opencv2/core.hpp>
#include <cpr/cpr.h>
#include <boost/asio/awaitable.hpp>

#include "chunk/types/base_chunk.hpp"
#include "config/config.hpp"
#include "utils/color_lib.hpp"
#include "utils/utils.hpp"
#include "utils/plot.hpp"
#include "chunk/chunk.hpp"

asio::awaitable<std::optional<std::string>> BaseChunk::update(const std::shared_ptr<const CFAsyncClient> cfCli) {

    co_await uploadParts(cfCli);
    co_await uploadImages(cfCli);

    // sample points for parent chunk to use
    std::mt19937 rng{std::random_device{}()};
    std::vector<cv::Mat> points;
    std::vector<uint16_t> colidxs;

    for (auto& [plotId, part] : _parts) {
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

        if (!build.size())
            continue;
            
        const size_t sampleCount = std::max(1ul, static_cast<size_t>(static_cast<float>(build.size()) * VARS::PC_SAMPLE_PERC));
        std::shuffle(build.begin(), build.end(), rng);

        const uint32_t plotIdInt = stoi(plotId, nullptr, 16);
        const uint32_t worldPosIdx = Chunk::plotIdToPosIdx(plotIdInt);
        const cv::Mat worldPos(1, 3, CV_32F, Utils::idxToVec3(worldPosIdx, VARS::MAIN_BUILD_SIZE).val);
        const uint16_t buildSize = buildData[1];

        for (size_t i = 0; i < sampleCount; ++i) {
            cv::Mat pos(1, 3, CV_32F, Utils::idxToVec3(build[i].first, buildSize).val);
            pos += 0.5f;
            pos /= buildSize;
            pos += worldPos;

            points.emplace_back(std::move(pos));
            colidxs.emplace_back(build[i].second);
        }
    }

    cv::Mat pointCloud;
    cv::vconcat(points, pointCloud);

    const uint32_t n = points.size();
    const size_t matSize = pointCloud.total() * pointCloud.elemSize();
    const size_t colSize = points.size() * sizeof(uint16_t);
 
    std::vector<uint8_t> buf;
    buf.reserve(sizeof(uint32_t) + matSize + colSize);

    std::memcpy(buf.data(), &n, sizeof(uint32_t));
    std::memcpy(buf.data() + sizeof(uint32_t), pointCloud.data, matSize);
    std::memcpy(buf.data() + sizeof(uint32_t) + matSize, colidxs.data(), colSize);

    auto out = co_await cfCli->putR2Object(
        VARS::CF_POINT_CLOUDS_BUCKET,
        _chunkId + ".dat",
        "application/octet-stream",
        buf
    );
    if (out.err)
        throw std::runtime_error(out.errMsg);

    // make next update chunk id
    const auto splitId = Chunk::parseIdStr(_chunkId);
    const auto nextLocId = Chunk::mapBwd(1, splitId.second);
    const auto nextChunkId = Chunk::makeIdStr(1, nextLocId, true);

    co_return nextChunkId;
}
