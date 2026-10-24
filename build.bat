@echo off
mkdir build
cd build
cmake .. -A x64 -G "Visual Studio 18 2026"
cmake --build . --config Release
cd ..