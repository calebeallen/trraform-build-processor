#pragma once

#include <string>

namespace VARS {

    inline constexpr int MAX_INFLIGHT = 20;

    inline constexpr int KMEANS_MAX_CLUSTERS = 5;
    inline constexpr int KMEANS_MAX_ITERS = 5;

    inline constexpr int PLOT_COUNT = 24;
    inline constexpr int D0_PLOT_COUNT = 34998;
    inline constexpr int BUILD_SIZE_STD = 48;
    inline constexpr int BUILD_SIZE_LRG = 72;

    inline constexpr int MAIN_BUILD_SIZE = 115;
    inline constexpr int L0_SIZE = 87;
    inline constexpr int L1_SIZE = 7571;
    inline constexpr int L2_SIZE = D0_PLOT_COUNT;
    inline constexpr float PC_SAMPLE_PERC = 0.1F;

    inline const std::string ORIGIN = "http://localhost:5173";
    inline const std::string CF_ZONE_ID = "64097c6d2cf0e0810ca05cdf8d4d1273";
    inline const std::string CF_CHUNKS_BUCKET = "chunks-dev";
    inline const std::string CF_PLOTS_BUCKET = "plots-dev";
    inline const std::string CF_IMAGES_BUCKET = "images-dev";
    inline const std::string CF_POINT_CLOUDS_BUCKET ="point-clouds-dev";

    inline const std::string REDIS_UPDATE_QUEUE_PREFIX = "up:q:0"; // may add multi-level queue later
    inline const std::string REDIS_UPDATE_NEEDS_UPDATE_PREFIX = "up:nu:";
    inline const std::string REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX = "up:nu:f:";
    inline const std::string REDIS_FLAG_UPDATE_METADATA_FIELDS_ONLY = "mfo";
    inline const std::string REDIS_FLAG_SET_DEFAULT_PLOT = "sdp";
    inline const std::string REDIS_FLAG_SET_DEFAULT_BUILD = "sdb";
    inline const std::string REDIS_FLAG_NO_IMAGE_UPDATE = "niu";

}