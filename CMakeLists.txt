cmake_minimum_required(VERSION 3.15)
project(MyProject)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Packages
find_package(Threads REQUIRED)                         # for -pthread etc.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(AWSSDK REQUIRED COMPONENTS s3)
find_package(cpr CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs highgui)

# Sources
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Linking (minimal)
target_link_libraries(${PROJECT_NAME} PRIVATE
  Threads::Threads
  nlohmann_json::nlohmann_json
  ${AWSSDK_LINK_LIBRARIES}
  cpr::cpr
  opencv_core
  opencv_imgproc
  opencv_imgcodecs
  opencv_highgui
)

if(APPLE)
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()
