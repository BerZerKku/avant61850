cmake_minimum_required(VERSION 2.8)

set(ARCH arm)
set(KERNEL ${CMAKE_CURRENT_SOURCE_DIR}/../linux-raspbian-3.18.11)

project(xr7-drivers)

add_definitions(-D__KERNEL__ -DMODULE)

add_executable(${PROJECT_NAME}
    ttyUart0/ttyUart0m
    ttyUart1/ttyUart1m
)

include_directories(
    ${KERNEL}/
    ${KERNEL}/include/
    ${KERNEL}/arch/${ARCH}/include
)

message(STATUS "KERNEL = ${KERNEL}")
