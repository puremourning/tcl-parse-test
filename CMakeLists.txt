cmake_minimum_required(VERSION 3.10)

# set the project name
project(TclAnalyzer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(TCL 8.6 REQUIRED)

# add the executable
add_subdirectory(src)
