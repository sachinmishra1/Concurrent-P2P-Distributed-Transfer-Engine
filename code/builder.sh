cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/engine --log-level=debug