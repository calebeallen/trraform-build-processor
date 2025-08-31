#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include <opencv2/core.hpp>
#include <sw/redis++/redis++.h>
#include <nlohmann/json.hpp>

#include "chunk/chunk_data.hpp"

typedef struct {
    bool updateMetadataFieldsOnly = false;
    bool setDefaultPlot = false;
    bool setDefaultBuild = false;
    bool noImageUpdate = false;
} UpdateFlags;

class DChunk : public ChunkData {

protected:
    std::unordered_map<uint64_t,std::vector<std::uint8_t>> _updatedJpegs;
    std::vector<UpdateFlags> _updateFlags;
   
    void downloadPlotUpdates();

public:
    DChunk(std::string, std::vector<std::uint64_t>, std::vector<UpdateFlags>);

    virtual void prep() override;
    void process() override;
    virtual std::optional<std::string> update() override;
    
};
