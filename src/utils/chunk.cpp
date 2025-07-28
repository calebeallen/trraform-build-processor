
#include "chunk.hpp"
#include <array>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>


// basic function for loading, uploading, encoding, decoding lowres chunks
// stores point clouds for each child chunk
// stores id
// stores layer
// constructor calls load
// load
//      use chunk map (depending on layer) to get child ids for chunk id
//      load all of the point clouds for child chunk ids from fs in memory
// update
//      run kmeans on point clouds, saves box geometry data to r2 bucket
// save
//      samples points and saves them to fs under chunk id (unless layer 0)


static const std::vector<int>& getMapped(const int layer, const int locId){

    static const std::array<std::vector<int>, L1_MAP_SIZE>& l1Map = []() {
        std::array<std::vector<int>, L1_MAP_SIZE> map;

        std::ifstream file("maps/l1.bin", std::ios::binary);
        if (!file) 
            throw std::runtime_error("Failed to open l1.bin");

        int buf[2];
        while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
            map[buf[0]].push_back(buf[1]);
        }

        return map;
    }();
    
    static const std::array<std::vector<int>, L2_MAP_SIZE>& l2Map = []() {
        std::array<std::vector<int>, L2_MAP_SIZE> map;

        std::ifstream file("maps/l2.bin", std::ios::binary);
        if (!file) 
            throw std::runtime_error("Failed to open l2.bin");

        int buf[2];
        while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
            map[buf[0]].push_back(buf[1]);
        }

        return map;
    }();

    if(layer == 1)
        return l1Map[locId];

    return l2Map[locId];

}

Chunk::Chunk(const std::string id) {

    std::stringstream ss(id);
    std::string layerStr, locIdStr;
    getline(ss, layerStr, '_');
    getline(ss, locIdStr, '_');

    layerStr.erase(0,1);
    layer = stoi(layerStr);
    locId = stoi(locIdStr);
    
    load();

}

void Chunk::load(){

    

}