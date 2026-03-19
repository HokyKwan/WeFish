cmake_minimum_required(VERSION 3.0)
project(wefish)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if(CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pthread")
endif()

#add_subdirectory(server)
#add_subdirectory(client)
#add_subdirectory(fserver)
#add_subdirectory(fclient)
add_subdirectory(assist)

