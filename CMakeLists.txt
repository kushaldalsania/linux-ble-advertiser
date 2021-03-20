cmake_minimum_required(VERSION 3.10.2)

project(advertiser)

add_executable(${CMAKE_PROJECT_NAME} InfoAdvertiser.c)
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC pthread)
