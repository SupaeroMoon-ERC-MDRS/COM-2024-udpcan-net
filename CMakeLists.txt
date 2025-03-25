cmake_minimum_required(VERSION 3.22.0)    

project(udpcan-net CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(CMAKE_CXX_FLAGS "-Wall")
set(CMAKE_CXX_FLAGS_DEBUG "-g3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

set(INC_DIRS
    ./include/
)

set(SOURCES
    ./src/net.cpp
)

set(TEST_SOURCES
    ./test/test.cpp
)

################## lib ##################

add_library(udpcan-net STATIC ${SOURCES})

set_target_properties(udpcan-net PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "../lib/"
    LIBRARY_OUTPUT_DIRECTORY "../lib/"
    RUNTIME_OUTPUT_DIRECTORY "../lib/"
)

target_include_directories(udpcan-net PRIVATE
    ${INC_DIRS}
)

if (WIN32)
    target_link_libraries(udpcan-net PRIVATE
        wsock32
    )
endif()

target_link_directories(udpcan-net PRIVATE

)

################## test ##################

add_executable(udpcan-net-test ${SOURCES} ${TEST_SOURCES})

target_include_directories(udpcan-net-test PRIVATE
    ${INC_DIRS}
)

if (WIN32)
    target_link_libraries(udpcan-net-test PRIVATE
        wsock32
    )
endif()

target_link_directories(udpcan-net-test PRIVATE

)