#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include <opencv2/opencv.hpp>
#include <sw/redis++/redis++.h>
#include <nlohmann/json.hpp>

#include "chunk_data.hpp"

typedef struct {
    bool setDefaultBuild = false;
    bool noImageUpdate = false;
} UpdateFlags;

class DChunk : public ChunkData {

protected:
    std::unordered_map<uint64_t,std::vector<std::uint8_t>> _updatedJpegs;
    std::vector<UpdateFlags> _updateFlags;

    // plot helpers (maybe move to sep namespace?)
    static const std::vector<uint8_t>& getDefaultBuildData();
    static const std::span<uint16_t> getBuildData(std::vector<std::uint8_t>&);
    static int getBuildSize(const std::vector<std::uint8_t>&);
    static void setPlotDataBuild(std::vector<std::uint8_t>&, const std::vector<uint8_t>&);
    static void setPlotDataJson(std::vector<std::uint8_t>&, const nlohmann::json&);
    static int getLocPlotId(const int);
    static int getLocPlotId(const std::string&);

    void downloadPlotUpdates();

public:
    DChunk(sw::redis::Redis&,std::string&);

    void process() override;
    void update() override;
    
};
