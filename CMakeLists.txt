cmake_minimum_required(VERSION 3.12)
project(student_agent_module)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Ofast -flto -march=native")

find_package(pybind11 REQUIRED)

pybind11_add_module(student_agent_module student_agent.cpp)