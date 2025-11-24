#pragma once

#include <string>

// make env
namespace CONFIG {
    inline constexpr size_t PIPELINE_LIMIT = 25;
    inline constexpr size_t REDIS_CONNECTIONS = 4;
    inline constexpr size_t R2_CONNECTIONS = 50;
    // inline constexpr int64_t L1_UPDATE_DELAY_SEC = 300; // 10 mins
    // inline constexpr int64_t L0_UPDATE_DELAY_SEC = 3600; //1 hour
    inline constexpr int64_t L1_UPDATE_DELAY_SEC = 1;
    inline constexpr int64_t L0_UPDATE_DELAY_SEC = 1;
}

namespace VARS {
    inline constexpr int KMEANS_MAX_ITERS = 5;

    inline constexpr int PLOT_COUNT = 24;
    inline constexpr int D0_PLOT_COUNT = 34998;
    inline constexpr int BUILD_SIZE_STD = 48;
    inline constexpr int BUILD_SIZE_LRG = 72;

    inline constexpr int MAIN_BUILD_SIZE = 115;
    inline constexpr int L0_SIZE = 87;
    inline constexpr int L1_SIZE = 7571;
    inline constexpr int L2_SIZE = D0_PLOT_COUNT;
    inline constexpr float PC_SAMPLE_PERC = 0.2F;

    inline constexpr auto ORIGIN = "http://localhost:5173";
    inline constexpr auto CF_ZONE_ID = "64097c6d2cf0e0810ca05cdf8d4d1273";
    inline constexpr auto CF_CHUNKS_BUCKET = "chunks-dev";
    inline constexpr auto CF_PLOTS_BUCKET = "plots-dev";
    inline constexpr auto CF_IMAGES_BUCKET = "build-images-dev";
    inline constexpr auto CF_POINT_CLOUDS_BUCKET ="point-clouds-dev";

    inline constexpr auto REDIS_EXPIRE = "1800"; // 30 mins
    inline constexpr auto REDIS_UPDATE_QUEUE_PREFIX = "up:q:0"; // may add multi-level queue later
    inline constexpr auto REDIS_UPDATE_NEEDS_UPDATE_PREFIX = "up:nu:";
    inline constexpr auto REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX = "up:nu:f:";
    inline constexpr auto REDIS_FLAG_METADATA_ONLY = "mo";
    inline constexpr auto REDIS_FLAG_SET_DEFAULT_JSON = "sdj";
    inline constexpr auto REDIS_FLAG_SET_DEFAULT_BUILD = "sdb";
    inline constexpr auto REDIS_FLAG_NO_IMAGE_UPDATE = "niu";
}