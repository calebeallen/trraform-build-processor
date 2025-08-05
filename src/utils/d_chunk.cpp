#include <span>
#include <vector>
#include <cstdint>
#include <stdexcept>

#include <opencv2/core.hpp>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>

#include "d_chunk.hpp"
#include "constants.hpp"
#include "cf_util.hpp"
#include "make_build_image.hpp"

const std::span<uint16_t> DChunk::getBuildData(std::vector<uint8_t>& plotData){

    // read json len metadata, skip it
    size_t i = static_cast<size_t>(plotData[0]) |
        (static_cast<size_t>(plotData[1]) << 8) |
        (static_cast<size_t>(plotData[2]) << 16) |
        (static_cast<size_t>(plotData[3]) << 24);
    i += 4;

    size_t buildLen = static_cast<size_t>(plotData[i]) |
        (static_cast<size_t>(plotData[i+1]) << 8) |
        (static_cast<size_t>(plotData[i+2]) << 16) |
        (static_cast<size_t>(plotData[i+3]) << 24);
    i += 4;

    uint8_t* u8 = plotData.data() + i;
    uint16_t* u16 = reinterpret_cast<uint16_t*>(u8);

    return std::span<uint16_t>(u16, buildLen / 2);
 
}

cv::Mat DChunk::idxToPos(const int idx, const int s) {

    const int s2 = s * s;
    cv::Mat pos(1, 3, CV_32F);
    pos.at<float>(0,0) = idx % s;
    pos.at<float>(0,1) = idx / s2;
    pos.at<float>(0,2) = (idx % s2) / s;
    return pos;

}

int DChunk::getLocPlotId(const int plotId) {
    return plotId <= VARS::PLOT_COUNT ? 0 : plotId & 0xFFF;
}

int DChunk::getLocPlotId(const std::string& plotIdStr) {
    return getLocPlotId(stoi(plotIdStr, nullptr, 16));
}

void DChunk::pullPlotUpdates(){

    // request all updated plots
    const size_t n = _needsUpdate.size();
    std::vector<Aws::S3::Model::GetObjectOutcomeCallable> futures(n);
    for (size_t i = 0; i < n; ++i) {
        auto& plotId = _needsUpdate[i];
        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(VARS::CF_PLOTS_BUCKET);
        request.SetKey(plotId + ".dat");
        futures[i] = r2Cli->GetObjectCallable(request);
    }

    // set new plot data, use plot's local id for chunk part key.
    for (size_t i = 0; i < n; ++i) {
        std::string& plotId = _needsUpdate[i];
        auto res = futures[i].get();
        if (!res.IsSuccess()) 
            continue;

        auto& stream = res.GetResultWithOwnership().GetBody();
        std::vector<uint8_t> data{
            std::istreambuf_iterator<char>(stream), 
            std::istreambuf_iterator<char>()
        };
    
        int locId = getLocPlotId(plotId);
        _parts[locId] = std::move(data);
    }

}

void DChunk::process() {

    // assuming by this point parts contains updated info
    // create new images for plots that need update
    for(const auto& plotId : _needsUpdate){
        const auto& buildData = getBuildData(_parts[getLocPlotId(plotId)]);
        _updatedJpegs[plotId] = BuildImage::make(buildData);
    }

}