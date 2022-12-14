cmake_minimum_required(VERSION 3.10)

project(SimpleHttpServer
    DESCRIPTION "A simple web server that supports HTTP/1.1"
    LANGUAGES CXX)

set(THREADS_PREFER_PTHREAD_FLAG ON)
set (CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
find_package(Threads REQUIRED)

include_directories(src)

set(SRC_DIR src)
set(TEST_DIR test)

add_executable(SimpleHttpServer
    ${SRC_DIR}/main.cc
    ${SRC_DIR}/http_server.cc
    ${SRC_DIR}/http_message.cc
    ${SRC_DIR}/storage.cc
)

add_executable(test_SimpleHttpServer
    ${TEST_DIR}/main.cc
    ${SRC_DIR}/http_server.cc
    ${SRC_DIR}/http_message.cc
    ${SRC_DIR}/storage.cc
)

target_link_libraries(SimpleHttpServer PRIVATE Threads::Threads)
target_link_libraries(test_SimpleHttpServer PRIVATE Threads::Threads)
