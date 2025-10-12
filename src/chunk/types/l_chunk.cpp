#include <fstream>
#include <algorithm>
#include <string>
#include <iostream>
#include <random>
#include <numeric>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/stream_file.hpp>

#include "config/config.hpp"
#include "chunk/types/l_chunk.hpp"
#include "chunk/chunk.hpp"
#include "utils/color_lib.hpp"

asio::awaitable<void> LChunk::prep(const std::shared_ptr<const CFAsyncClient> cfCli){

    co_await downloadParts(cfCli);
    co_await downloadPointCloud(cfCli);

    // get updated point clouds
    std::vector<GetOutcome> updates;
    {
        std::vector<GetParams> requests;
        requests.reserve(_needsUpdateStr.size());
        for (const auto& id : _needsUpdateStr)
            requests.push_back({
                VARS::CF_POINT_CLOUDS_BUCKET,
                id
            });

        updates = co_await cfCli->getManyR2Objects(std::move(requests));
    }

    // sample updated point clouds
    for (size_t i = 0; i < updates.size(); ++i) {
        const auto& obj = updates[i];

        if (obj.err)
            throw std::runtime_error(obj.errMsg);

        uint32_t headerStart = 2 + sizeof(uint32_t);
        uint32_t headerLen;
        std::memcpy(&headerLen, obj.body.data() + 2, sizeof(uint32_t));

        uint32_t numPoints;
        std::memcpy(&numPoints, obj.body.data() + headerStart + headerLen, sizeof(uint32_t));

        uint32_t pointsStart = headerStart + headerLen + sizeof(uint32_t);
        uint32_t colorsStart = pointsStart + numPoints * sizeof(float) * 3;
        
        // shuffle indices for random sample
        std::vector<int> idx(numPoints);
        std::iota(idx.begin(), idx.end(), 0);
        std::mt19937 rng(std::random_device{}());
        std::shuffle(idx.begin(), idx.end(), rng);
        
        // create matrix
        const size_t k = std::max(1ul, static_cast<size_t>(static_cast<float>(numPoints) * VARS::PC_SAMPLE_PERC));
        cv::Mat points(k, 3, CV_32F);
        std::vector<uint16_t> colors(k);

        for (int j = 0; j < k; ++j) {
            // copy point
            std::memcpy(
                points.ptr<float>(j),
                obj.body.data() + pointsStart + idx[j] * sizeof(float) * 3,
                sizeof(float) * 3
            );
            // copy color idx
            std::memcpy(
                &colors[j],
                obj.body.data() + colorsStart + idx[j] * sizeof(uint16_t),
                sizeof(uint16_t)
            );
        }

        _pointClouds.try_emplace(_needsUpdate[i], std::move(points), std::move(colors));
    }

    // save updated point cloud (not modified from here)
    co_await uploadPointCloud(cfCli);
}

void LChunk::process(){

    // compute low-resolution representations of the chunk
    for (const auto& id : _needsUpdate) {
        const PointCloud& pointCloud = _pointClouds[id];
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

        _parts[id] = std::move(buf);
    }

}

asio::awaitable<std::optional<std::string>> LChunk::update(const std::shared_ptr<const CFAsyncClient> cfCli) {

    co_await uploadParts(cfCli);

    // create parent chunk id for update
    const auto [layer, locId] = Chunk::parseIdStr(_chunkId);
    if (layer == 0)
        co_return std::nullopt;

    const int nextLocId = Chunk::mapBwd(layer - 1, locId);
    co_return Chunk::makeIdStr(1, nextLocId, true);
}

boost::asio::awaitable<void> LChunk::downloadPointCloud(const std::shared_ptr<const CFAsyncClient> cfCli) {

    auto obj = co_await cfCli->getR2Object(VARS::CF_POINT_CLOUDS_BUCKET, _chunkId); 
    if (obj.err) {
        if (obj.errType != Aws::S3::S3Errors::NO_SUCH_KEY)
            throw std::runtime_error(obj.errMsg);
        co_return;
    }

    /*
        Format
        | 2 bytes reserved | headerlen (4 bytes) |  header (uint64_t arr) | numpoints (4 bytes) | points (float arr) | coloridxs (uint16_t arr) |
        header: | len (uint32_t)| content: list of ids and their corresponding content's offset and len (ex: [1,120,32]) |
    */

    uint32_t headerStart = 2 + sizeof(uint32_t);
    uint32_t headerLen;
    std::memcpy(&headerLen, obj.body.data() + 2, sizeof(uint32_t));

    uint32_t numPoints;
    std::memcpy(&numPoints, obj.body.data() + headerStart + headerLen, sizeof(uint32_t));

    uint32_t pointsStart = headerStart + headerLen + sizeof(uint32_t);
    uint32_t colorsStart = pointsStart + numPoints * sizeof(float) * 3;

    // only keep parts that do not need update
    std::unordered_set<uint64_t> nuSet(_needsUpdate.begin(), _needsUpdate.end());

    uint32_t i = headerStart;
    while (i < pointsStart) {
        uint64_t id;
        std::memcpy(&id, obj.body.data() + i, sizeof(uint64_t));

        // TODO skip if item in needs update
        if(!nuSet.contains(id)) {
            uint32_t offset, len;
            std::memcpy(&offset, obj.body.data() + i + sizeof(uint64_t), sizeof(uint32_t));
            std::memcpy(&len, obj.body.data() + i + sizeof(uint64_t) + sizeof(uint32_t), sizeof(uint32_t));

            // create matrix
            cv::Mat points(len, 3, CV_32F);
            std::vector<uint16_t> colidxs(len);

            std::memcpy(points.data, obj.body.data() + pointsStart + points.elemSize() * offset * 3, points.total() * points.elemSize());
            std::memcpy(colidxs.data(), obj.body.data() + colorsStart + sizeof(uint16_t) * offset, sizeof(uint16_t)*len);

            _pointClouds.try_emplace(id, std::move(points), std::move(colidxs));
        }
        i += sizeof(uint64_t) * 2;
    }
}

boost::asio::awaitable<void> LChunk::uploadPointCloud(const std::shared_ptr<const CFAsyncClient> cfCli) const {

    if (_pointClouds.size() == 0)
        co_return;

    // allocate space for header + reserved
    uint32_t headerLen = _pointClouds.size() * sizeof(uint64_t) * 2;
    std::vector<uint8_t> buf(headerLen + sizeof(uint32_t) + 2);
    std::memcpy(buf.data() + 2, &headerLen, sizeof(uint32_t));

    uint32_t pointOffset = 0;
    uint32_t i = 2 + sizeof(uint32_t);

    for (const auto& [id, pointCloud] : _pointClouds) {
        uint32_t numPoints = pointCloud.points.rows;
        std::memcpy(buf.data() + i, &id, sizeof(uint64_t));
        i += sizeof(uint64_t);
        std::memcpy(buf.data() + i, &pointOffset, sizeof(uint32_t));
        i += sizeof(uint32_t);
        std::memcpy(buf.data() + i, &numPoints, sizeof(uint32_t));
        i += sizeof(uint32_t);
        pointOffset += numPoints;
    }

    // allocate space for numpoints prefix and body (points and color idxs)
    buf.resize(buf.size() + sizeof(uint32_t) + sizeof(float)*3*pointOffset + sizeof(uint16_t)*pointOffset);
    std::memcpy(buf.data() + i, &pointOffset, sizeof(uint32_t));
    uint32_t pointsStart = i + sizeof(uint32_t);
    uint32_t colorsStart = pointsStart + sizeof(float)*3*pointOffset;
    i = 0;

    for (const auto& [_, pointCloud] : _pointClouds) {
        std::memcpy(
            buf.data() + pointsStart + i * pointCloud.points.elemSize() * 3, 
            pointCloud.points.data, 
            pointCloud.points.total() * pointCloud.points.elemSize()
        );
        std::memcpy(
            buf.data() + colorsStart + i * sizeof(uint16_t),
            pointCloud.colidxs.data(),
            pointCloud.colidxs.size() * sizeof(uint16_t)
        );
        i += pointCloud.points.rows;
    }

    auto out = co_await cfCli->putR2Object(
        VARS::CF_POINT_CLOUDS_BUCKET,
        _chunkId,
        "application/octet-stream",
        buf
    );
    if (out.err)
        throw std::runtime_error(out.errMsg);
}
