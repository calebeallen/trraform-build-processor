
cmake_minimum_required(VERSION 3.20)
project(trrasvr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

find_package(fmt CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system asio redis)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)
find_package(AWSSDK CONFIG REQUIRED COMPONENTS s3)

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# public headers in ./include
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    fmt::fmt
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    Boost::system
    cpr::cpr
    nlohmann_json::nlohmann_json
    ${OpenCV_LIBS}
    ${AWSSDK_LINK_LIBRARIES}
)

# compile optimizations
target_compile_options(${PROJECT_NAME} PRIVATE
    -O0                      # No optimization
    -g3                      # Fullest debug info
    -Wall                    # Enable all warnings
    -Wextra                  # Enable extra warnings
    -Wpedantic               # Enable pedantic warnings
    -fsanitize=address       # <<< ENABLE ADDRESS SANITIZER
)

# linker optimizations
target_link_options(${PROJECT_NAME} PRIVATE
    -flto=auto
    -fsanitize=address
)


set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

