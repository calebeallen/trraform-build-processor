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

constexpr size_t PC_ENCODED_HEADER_ENTRY_SIZE = sizeof(uint64_t) + sizeof(uint32_t);
constexpr size_t VEC3F_SIZE = sizeof(float) * 3;
constexpr size_t COLOR_IDX_SIZE = sizeof(uint16_t);

asio::awaitable<void> LChunk::prep(const std::shared_ptr<CFAsyncClient> cfCli) {
    co_await downloadParts(cfCli);
    co_await downloadPointCloud(cfCli);

    // get updated point clouds
    std::vector<CFAsyncClient::GetOutcome> updates;
    {
        std::vector<CFAsyncClient::GetParams> requests;
        requests.reserve(_needsUpdate.size());
        for (const auto& id : _needsUpdate) 
            requests.push_back({
                VARS::CF_POINT_CLOUDS_BUCKET,
                Chunk::makeIdStr(_idl + 1, id, true),
                false, // head only
                true // use cache
            });
        
        updates = co_await cfCli->getManyR2Objects(std::move(requests));
    }
    // sample updated point clouds
    for (size_t i = 0; i < updates.size(); ++i) {
        auto& obj = updates[i];
        if (obj.err)
            throw std::runtime_error(obj.errMsg);

        uint8_t* headerPtr = obj.body.data() + 2;
        uint32_t totalEntries, totalPoints;
        std::memcpy(&totalEntries, headerPtr, sizeof(uint32_t));
        headerPtr += sizeof(uint32_t);
        std::memcpy(&totalPoints, headerPtr, sizeof(uint32_t));
        headerPtr += sizeof(uint32_t);
        
        // shuffle indices for random sample
        std::vector<int> idx(totalPoints);
        std::iota(idx.begin(), idx.end(), 0);
        std::mt19937 rng(std::random_device{}());
        std::shuffle(idx.begin(), idx.end(), rng);
        
        // create matrix
        const size_t k = std::max(2ul, static_cast<size_t>(static_cast<float>(totalPoints) * VARS::PC_SAMPLE_PERC));
        cv::Mat points(k, 3, CV_32F);
        std::vector<uint16_t> colors(k);
         
        uint8_t* pntptr = headerPtr + totalEntries*PC_ENCODED_HEADER_ENTRY_SIZE;
        uint8_t* colptr = pntptr + totalPoints*VEC3F_SIZE;

        for (size_t j = 0; j < k; ++j) {
            // copy point
            std::memcpy(
                points.ptr<float>(j),
                pntptr + idx[j]*VEC3F_SIZE,
                sizeof(float) * 3
            );
            // copy color idx
            std::memcpy(
                &colors[j],
                colptr + idx[j]*COLOR_IDX_SIZE,
                sizeof(uint16_t)
            );
        }

        _pointClouds[_needsUpdate[i]] = PointCloud{std::move(points), std::move(colors)};
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
        size_t k = static_cast<size_t>(std::log10(static_cast<double>(n) + 1.)) + 1;

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

asio::awaitable<std::optional<std::string>> LChunk::update(const std::shared_ptr<CFAsyncClient> cfCli) {
    co_await uploadParts(cfCli);

    // create parent chunk id for update
    const auto [layer, locId] = Chunk::parseIdStr(_chunkId);
    if (layer == 0)
        co_return std::nullopt;

    const int nextLocId = Chunk::mapBwd(layer - 1, locId);
    co_return Chunk::makeIdStr(layer - 1, nextLocId, true);
}

boost::asio::awaitable<void> LChunk::downloadPointCloud(const std::shared_ptr<CFAsyncClient> cfCli) {
    // get object with cache
    auto obj = co_await cfCli->getR2Object(VARS::CF_POINT_CLOUDS_BUCKET, _chunkId, true); 
    if (obj.err) {
        if (obj.errType != Aws::S3::S3Errors::NO_SUCH_KEY)
            throw std::runtime_error(obj.errMsg);
        co_return;
    }

    // format: | total entries | total points | header: [id,len] | points | color indices
    uint8_t* headerPtr = obj.body.data() + 2;
    uint32_t totalEntries, totalPoints;
    std::memcpy(&totalEntries, headerPtr, sizeof(uint32_t));
    headerPtr += sizeof(uint32_t);
    std::memcpy(&totalPoints, headerPtr, sizeof(uint32_t));
    headerPtr += sizeof(uint32_t);

    uint8_t* pntptr = headerPtr + totalEntries*PC_ENCODED_HEADER_ENTRY_SIZE;
    uint8_t* colptr = pntptr + totalPoints*VEC3F_SIZE;
    
    // only keep parts that do not need update
    std::unordered_set<uint64_t> nuSet(_needsUpdate.begin(), _needsUpdate.end());

    for (size_t i = 0; i < totalEntries; ++i) {
        uint64_t id;
        std::memcpy(&id, headerPtr, sizeof(uint64_t));
        headerPtr += sizeof(uint64_t);
        uint32_t n;
        std::memcpy(&n, headerPtr, sizeof(uint32_t));
        headerPtr += sizeof(uint32_t);

        // TODO skip if item in needs update
        if(!nuSet.contains(id)) {
            // create matrix
            cv::Mat points(n, 3, CV_32F);
            std::vector<uint16_t> colidxs(n);

            std::memcpy(points.data, pntptr, n * VEC3F_SIZE);
            std::memcpy(colidxs.data(), colptr, n * COLOR_IDX_SIZE);

            _pointClouds[id] = PointCloud{std::move(points), std::move(colidxs)};
        }

        pntptr += n * VEC3F_SIZE;
        colptr += n * COLOR_IDX_SIZE;
    }    
}

boost::asio::awaitable<void> LChunk::uploadPointCloud(const std::shared_ptr<CFAsyncClient> cfCli) const {
    if (_pointClouds.size() == 0)
        co_return;

    uint32_t totalEntries = _pointClouds.size();

    assert(totalEntries > 0 && "Point cloud must have at least one entry");

    uint32_t totalPoints = 0;
    for (const auto& [_, pointCloud] : _pointClouds)
        totalPoints += pointCloud.points.rows;

    assert(totalPoints > 1 && "Point cloud must have at least 2 points");

    // allocate buffer, write len prefixes
    std::vector<uint8_t> buf(2 + 2*sizeof(uint32_t) + totalEntries*PC_ENCODED_HEADER_ENTRY_SIZE + totalPoints*(VEC3F_SIZE + COLOR_IDX_SIZE));
    buf[0] = buf[1] = 0;

    uint8_t* headptr = buf.data() + 2;
    std::memcpy(headptr, &totalEntries, sizeof(uint32_t));
    headptr += sizeof(uint32_t);
    std::memcpy(headptr, &totalPoints, sizeof(uint32_t));
    headptr += sizeof(uint32_t);

    uint8_t* pntptr = headptr + totalEntries*PC_ENCODED_HEADER_ENTRY_SIZE;
    uint8_t* colptr = pntptr + totalPoints*VEC3F_SIZE;

    // write data
    for (const auto& [id, pointCloud] : _pointClouds) {
        std::memcpy(headptr, &id, sizeof(uint64_t));
        headptr += sizeof(uint64_t);
        uint32_t n = pointCloud.points.rows;
        std::memcpy(headptr, &n, sizeof(uint32_t));
        headptr += sizeof(uint32_t);

        std::memcpy(
            pntptr, 
            pointCloud.points.data, 
            n * VEC3F_SIZE
        );
        std::memcpy(
            colptr,
            pointCloud.colidxs.data(),
            n * COLOR_IDX_SIZE
        );
        pntptr += n * VEC3F_SIZE;
        colptr += n * COLOR_IDX_SIZE;
    }

    auto out = co_await cfCli->putR2Object(
        VARS::CF_POINT_CLOUDS_BUCKET,
        _chunkId,
        "application/octet-stream",
        std::move(buf),
        true // use cache
    );
    if (out.err)
        throw std::runtime_error(out.errMsg);
}
