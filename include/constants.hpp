#pragma once
#include <string>

namespace VARS {

    inline constexpr int KMEANS_MAX_CLUSTERS = 5;
    inline constexpr int KMEANS_MAX_ITERS = 5;

    inline constexpr int PLOT_COUNT = 24;
    inline constexpr int D0_PLOT_COUNT = 34998;
    inline constexpr int BUILD_SIZE_STD = 48;
    inline constexpr int BUILD_SIZE_LRG = 72;

    inline constexpr int L0_SIZE = 87;
    inline constexpr int L1_SIZE = 7571;
    inline constexpr int L2_SIZE = D0_PLOT_COUNT;
    inline constexpr float PC_SAMPLE_PERC = 0.1F;

    inline const std::string CF_CHUNKS_BUCKET = "chunks";
    inline const std::string CF_PLOTS_BUCKET = "plots";

    inline const std::string REDIS_UPDATE_QUEUE_PREFIX = "up:q:";
    inline const std::string REDIS_UPDATE_NEEDS_UPDATE_PREFIX = "up:nu:";
    inline const std::string REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX = "up:nu:f";
    inline const std::string REDIS_NO_IMAGE_UPDATE_FLAG = "niu";
    inline const std::string REDIS_SET_DEFAULT_BUILD_FLAG = "sdb";

}