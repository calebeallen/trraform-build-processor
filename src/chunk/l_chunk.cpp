#include <fstream>
#include <algorithm>
#include <string>
#include <iostream>
#include <random>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/stream_file.hpp>

#include "config/config.hpp"
#include "chunk/l_chunk.hpp"
#include "utils/color_lib.hpp"

LChunk::LChunk(
    std::string chunkId, 
    std::vector<std::string> needsUpdate, 
    std::shared_ptr<CFAsyncClient> cfCli
) : ChunkData(std::move(chunkId), std::move(needsUpdate), std::move(cfCli)) {}

asio::awaitable<void> LChunk::prep(){

    co_await downloadParts();

    // get child ids of vector
    auto idParts = parseChunkIdStr(_chunkId);
    auto layer = std::get<0>(idParts);
    auto locId = std::get<1>(idParts);
    const std::vector<int>& childIds = getMappedFwd(layer, locId);

    
    std::vector<GetOutcome> results;
    {
        std::vector<GetParams> requests;
        requests.reserve(childIds.size());
        for (const auto id : childIds)
            requests.push_back({
                VARS::CF_POINT_CLOUDS_BUCKET,
                makeChunkIdStr(layer + 1, id, true) + ".dat"
            });

        results = co_await _cfCli->getManyR2Objects(std::move(requests));
    }

    for (size_t i = 0; i < childIds.size(); ++i) {
        const auto id = childIds[i];
        const auto& obj = results[i];

        if (obj.err)
            throw std::runtime_error(obj.errMsg);
        
        // read rows header 4 bytes
        uint32_t n;
        std::memcpy(&n, obj.body.data(), sizeof(uint32_t));

        // create matrix
        cv::Mat pointCloud(n, 3, CV_32F);
        std::vector<uint16_t> colidxs(n);
        const size_t matSize = pointCloud.total() * pointCloud.elemSize();

        std::memcpy(pointCloud.data, obj.body.data() + sizeof(uint32_t), matSize);
        std::memcpy(colidxs.data(),  obj.body.data() + sizeof(uint32_t) + matSize, sizeof(uint16_t) * n);

        _pointClouds.emplace(id, {
            std::move(pointCloud),
            std::move(colidxs)
        });
    }

}

void LChunk::process(){

    // compute low-resolution representations of the chunk
    for (const auto& locId : _needsUpdate) {
        const PointCloud& pointCloud = _pointClouds[locId];
        const cv::Mat& pnts = pointCloud.points;
        const size_t n = pnts.rows;

        if(pnts.rows < 2)
            continue;

        // compute bounds of point cloud
        cv::Mat min, max;
        cv::reduce(pnts, min, 0, cv::REDUCE_MIN);
        cv::reduce(pnts, max, 0, cv::REDUCE_MAX);

        // normalize points
        cv::Mat pntsNorm = pnts.clone();
        for (int i = 0; i < 3; ++i) {
            float m = min.at<float>(0,i);
            float r = max.at<float>(0,i) - m;
            if (r > 1e-6f) {
                pntsNorm.col(i) -= m;
                pntsNorm.col(i) /= r;
            }
        }

        // compute number of boxes
        double kLin = std::min(static_cast<double>(n) / static_cast<double>(std::pow(VARS::BUILD_SIZE_STD, 3)), 1.0);
        size_t k = static_cast<size_t>((1.0 - std::pow(kLin - 1.0, 6)) * VARS::KMEANS_MAX_CLUSTERS);
        k = std::clamp(k, 1ul, n);

        cv::Mat labels, centers;
        cv::kmeans(
            pntsNorm, k, labels,
            cv::TermCriteria(
                cv::TermCriteria::EPS+cv::TermCriteria::MAX_ITER, 
                VARS::KMEANS_MAX_ITERS, 
                1e-4
            ), 1, cv::KMEANS_PP_CENTERS, centers
        );

        // compute bounding boxes, encode into uint8 buffer
        std::vector<cv::Vec3f> mean(k, {0,0,0}), m2(k, {0,0,0}), color(k, {0,0,0});
        std::vector<float> count(k, 0);
        for (size_t i = 0; i < n; ++i) {
            size_t cl = labels.at<int>(i);
            const float* p = pnts.ptr<float>(i);
            count[cl] += 1.0f;

            cv::Vec3f col = *ColorLib::getColorAsVec(pointCloud.colidxs[i]);

            for (size_t j = 0; j < 3; ++j) {
                float x = p[j];
                float delta = x - mean[cl][j];
                mean[cl][j] += delta / count[cl];
                float delta2 = x - mean[cl][j];
                m2[cl][j] += delta * delta2;
                color[cl][j] += col[j];
            }   
        }

        // encode boxes into buffer
        std::vector<uint8_t> buf(9*k*sizeof(float));
        for (size_t i = 0; i < k; ++i) {
            const float clusterSize = count[i];
            if (clusterSize == 0)
                continue;

            cv::Vec3f stddev;
            for (size_t j = 0; j < 3; ++j)
                stddev[j] = clusterSize > 1.0f ? std::sqrt(m2[i][j] / (clusterSize - 1)) : 0.0f;

            float arr[9]{
                mean[i][0] - stddev[0], mean[i][1] - stddev[1], mean[i][2] - stddev[2],
                mean[i][0] + stddev[0], mean[i][1] + stddev[1], mean[i][2] + stddev[2],
                color[i][0] / clusterSize, color[i][1] / clusterSize, color[i][2] / clusterSize,
            };

            std::memcpy(buf.data() + i*9*sizeof(float), &arr[0], sizeof(float)*9);
        }

        _parts[locId] = std::move(buf);
    }

}

asio::awaitable<std::optional<std::string>> LChunk::update() {

    co_await uploadParts();

    const auto splitId = ChunkData::parseChunkIdStr(_chunkId);
    int layer = std::get<0>(splitId);
    if (layer == 0)
        co_return std::nullopt;

    // sample points for parent chunk to use
    std::vector<cv::Mat> samples;
    std::mt19937 rng{std::random_device{}()};

    for(const auto& [_, mat] : _pointClouds){
        size_t n = mat.rows;
        size_t sampleCount = std::max(1ul, static_cast<size_t>(n * VARS::PC_SAMPLE_PERC));

        std::vector<size_t> idxs(n);
        for (size_t i = 0; i < n; ++i)
            idxs[i] = i;

        std::shuffle(idxs.begin(), idxs.end(), rng);

        for (size_t i = 0; i < sampleCount; ++i)
            samples.push_back(mat.row(idxs[i]));
    }

    cv::Mat pointCloud;
    cv::vconcat(samples, pointCloud);

    const std::uint32_t rows = pointCloud.rows;
    const size_t size = pointCloud.total() * pointCloud.elemSize();
    std::vector<std::uint8_t> data(sizeof(std::uint32_t) + size);
    std::memcpy(data.data(), &rows, sizeof(std::uint32_t));
    std::memcpy(data.data() + sizeof(std::uint32_t), pointCloud.data, size);

    auto out = co_await _cfCli->putR2Object(
        VARS::CF_POINT_CLOUDS_BUCKET,
        _chunkId + ".dat",
        "application/octet-stream",
        data
    );
    if (out.err)
        throw std::runtime_error(out.errMsg);

    // create parent chunk id for update
    const int nextLocId = getMappedBwd(layer - 1, std::get<1>(splitId));
    co_return makeChunkIdStr(1, nextLocId, true);

}