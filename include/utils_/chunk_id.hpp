#pragma once

#include <cstdint>
#include <vector>
#include <optional>

class ChunkId {

public:
    static const std::vector<uint32_t>& map_fwd(size_t layer, size_t idx);
    static uint32_t map_bwd(size_t layer, size_t idx);

    ChunkId(const std::string& chunk_id);

    const std::string& get_string();
    std::optional<std::string> get_parent();

private:
    std::string chunk_id_;
    bool lod_chunk_ = false;
    uint64_t idl_, idr_;

};