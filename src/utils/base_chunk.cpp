#include <vector>
#include <span>
#include <utility>

#include <opencv2/core.hpp>

#include "base_chunk.hpp"
#include "color_lib.hpp"

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
