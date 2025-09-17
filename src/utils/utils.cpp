#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cctype>

#include "utils/utils.hpp"

cv::Vec3f Utils::idxToVec3(const int idx, const int bs) {
    const int bs2 = bs * bs;
    return cv::Vec3f{
        float(idx % bs),
        float(idx / bs2),
        float((idx % bs2) / bs)
    };
};

cv::Vec4f Utils::idxToVec4(const int idx, const int bs) {
    const int bs2 = bs * bs;
    return cv::Vec4f{
        float(idx % bs),
        float(idx / bs2),
        float((idx % bs2) / bs),
        1.0
    };
};

void Utils::loadENV(const std::string& path) {

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Could not open .env file: " << path << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {

        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            // trim trailing/leading spaces
            while (!key.empty() && std::isspace(key.back())) 
                key.pop_back();
            while (!value.empty() && std::isspace(value.back())) 
                value.pop_back();
            while (!key.empty() && std::isspace(key.front())) 
                key.erase(0,1);
            while (!value.empty() && std::isspace(value.front())) 
                value.erase(0,1);

            if (setenv(key.c_str(), value.c_str(), 1) != 0)
                perror(("setenv failed for " + key).c_str());
                
        }
    }
    
}
