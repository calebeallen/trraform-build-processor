cmake_minimum_required(VERSION 3.16)
project(your_project_name)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the policy for newer Boost versions
cmake_policy(SET CMP0167 NEW)

# Help CMake find packages
set(CMAKE_PREFIX_PATH /opt/homebrew /usr/local ${CMAKE_PREFIX_PATH})

# Find all packages
find_package(AWSSDK REQUIRED COMPONENTS s3)
find_package(Boost REQUIRED COMPONENTS redis)
find_package(cpr REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)

# Find Boost Redis library manually
find_library(BOOST_REDIS_LIB 
    NAMES boost_redis libboost_redis
    PATHS /opt/homebrew/lib /usr/local/lib
    NO_DEFAULT_PATH
)

# Collect source files
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.h" "include/*.hpp")

# Create executable
add_executable(your_app ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(your_app 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${AWSSDK_INCLUDE_DIRS}
        ${OpenCV_INCLUDE_DIRS}
        /opt/homebrew/include
)

# Link libraries
target_link_libraries(your_app 
    ${AWSSDK_LINK_LIBRARIES}
    Boost::boost
    Boost::redis
    cpr::cpr
    nlohmann_json::nlohmann_json
    ${OpenCV_LIBS}
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Add Boost Redis if found
if(BOOST_REDIS_LIB)
    target_link_libraries(your_app ${BOOST_REDIS_LIB})
    message(STATUS "Found Boost Redis: ${BOOST_REDIS_LIB}")
else()
    message(FATAL_ERROR "Boost Redis library not found. Make sure it's installed.")
endif()

# Add threading support
find_package(Threads REQUIRED)
target_link_libraries(your_app Threads::Threads)

# Print debug info
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")