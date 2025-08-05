#include <vector>
#include <span>
#include <utility>
#include <tuple>

#include <opencv2/core.hpp>

#include "base_chunk.hpp"
#include "constants.hpp"
#include "color_lib.hpp"
#include "utils.hpp"

void BaseChunk::savePointCloud(){

    cv::RNG rng;
    std::vector<cv::Mat> points;

    for (auto& [key, part] : _parts) {
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

        

        for (size_t i = 0; i < sampleCount; ++i) {

            cv::Mat pos(1, 3, CV_32F, Utils::idxToVec3(std::get<0>(vox[i]), bs).val);
            cv::Mat col(*ColorLib::getColor(std::get<1>(vox[i])));
            pos /= bs;
            


        }

    }

}

// void BaseChunk::loadPointClouds(){

//     // create point clouds from build data
//     // maybe add sample percentage param?
//     for (auto& [key, part] : _parts) {
//         const auto buildData = getBuildData(part);
//         const int buildSize = buildData[1];
//         const size_t n = buildData.size();
//         int idx = 0;

//         std::vector<cv::Mat> vects;
//         cv::Mat color;
        
//         for (size_t i = 0; i < n; ++i) {
//             if (buildData[i] & 1) {
//                 color = ColorLib::getColor(buildData[i] >> 1);
//                 if(!color.empty()){
//                     cv::Mat pos(idxToPos(idx, buildSize));
//                     cv::Mat merged;
//                     cv::hconcat(pos, color, merged);
//                     vects.push_back(std::move(merged));
//                 }
//                 ++idx;
//             } else {
//                 const size_t repeat = buildData[i];
//                 if(color.empty())
//                     for(size_t j = 0; j < repeat; ++j){
//                         cv::Mat pos(idxToPos(idx + j, buildSize));
//                         cv::Mat merged;
//                         cv::hconcat(pos, color, merged);
//                         vects.push_back(std::move(merged));
//                     }
//                 idx += repeat;
//             }
//         }

//         cv::Mat pointCloud;
//         cv::vconcat(vects, pointCloud);
//         _pointClouds[key] = std::move(pointCloud);
//     }

// }
