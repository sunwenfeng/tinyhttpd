cmake_minimum_required(VERSION 3.6)
project(tinyhttpd_0_1_0)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
    httpd.c
    simpleclient.c)

add_executable(tinyhttpd_0_1_0 ${SOURCE_FILES})