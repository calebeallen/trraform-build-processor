#include <span>
#include <cstdint>

#include <opencv2/core.hpp>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>

#include "d_chunk.hpp"
#include "constants.hpp"
#include "cf_util.hpp"

const std::span<uint8_t> DChunk::getBuildData(std::vector<uint8_t>& plotData){

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

    return std::span<uint8_t>(plotData.data() + i, buildLen);
 
}

void DChunk::pullPlotUpdates(){

    // request all updated plots
    std::vector<Aws::S3::Model::GetObjectOutcomeCallable> futures;
    for (const auto& plotId : needsUpdate) {
        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(CF_PLOTS_BUCKET);
        request.SetKey(plotId + ".dat");
        futures.emplace_back(r2Cli->GetObjectCallable(request));
    }

    for (auto& future : futures) {
        auto res = future.get();

        if (res.IsSuccess()) {
            auto& result = res.GetResult();
            auto& body = result.GetBody();
        } 
    }

}
