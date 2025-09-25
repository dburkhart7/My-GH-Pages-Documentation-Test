cmake_minimum_required(VERSION 3.13)
project(CANDOR_RESEARCH VERSION 1.0 LANGUAGES C CXX)
set(CMAKE_BUILD_TYPE Debug)

link_directories(/usr/lib/x86_64-linux-gnu)
include_directories(/usr/include)
include_directories(/usr/local)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/src)

## Zero MQ C++ ##
include(FetchContent)

FetchContent_Declare(
  cppzmq
  GIT_REPOSITORY https://github.com/zeromq/cppzmq
  GIT_TAG v4.10.0
)

# Disable cppzmq tests to avoid build errors
set(CPPZMQ_BUILD_TESTS OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(cppzmq)

## Argparse ##
FetchContent_Declare(
  argparse
  GIT_REPOSITORY https://github.com/p-ranav/argparse.git
  GIT_TAG v3.0
)

FetchContent_MakeAvailable(argparse)

## Quill ##
FetchContent_Declare(
    quill
    GIT_REPOSITORY https://github.com/odygrd/quill.git
    GIT_TAG        v7.5.0 # Use the latest stable version or the version you need
)

FetchContent_MakeAvailable(quill)

# Add nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3 # Use the latest stable version
)

FetchContent_MakeAvailable(nlohmann_json)

set(SERVICE_COMPONENTS "s3")

find_package(readline)
find_package(Eigen3 REQUIRED)
find_package(OpenCV REQUIRED)
find_package(k4a REQUIRED)
find_package(AWSSDK REQUIRED COMPONENTS ${SERVICE_COMPONENTS})

include_directories(${EIGEN3_INCLUDE_DIR})
include_directories(${OpenCV_INCLUDE_DIRS} ${SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/src)

## Build Executable(s)
add_executable(
  cns
  src/name_server/cns_main.cpp
  src/name_server/cns.cpp
  src/name_server/cns.hpp
)

add_executable(
  kinect
  src/kinect/kinect.cpp
)

add_executable(
  imview
  src/image_viewer.cpp
)

# make src directory available to each executable
target_include_directories(cns PRIVATE src)
target_include_directories(replay_jpeg PRIVATE src)
target_include_directories(kinect PRIVATE src)
target_include_directories(imview PRIVATE src)

target_link_libraries(cns cppzmq quill argparse nlohmann_json::nlohmann_json ${AWSSDK_LIBRARIES})
target_link_libraries(kinect k4a cppzmq quill argparse nlohmann_json::nlohmann_json ${OpenCV_LIBS})
target_link_libraries(imview k4a cppzmq argparse nlohmann_json::nlohmann_json ${OpenCV_LIBS})

# Install all executables
install(TARGETS cns
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(TARGETS kinect
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(TARGETS imview
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
