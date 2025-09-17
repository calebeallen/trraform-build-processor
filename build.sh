
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=/home/caleb/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j