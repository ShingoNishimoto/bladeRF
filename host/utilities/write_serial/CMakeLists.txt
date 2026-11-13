cmake_minimum_required(VERSION 2.8)
project(write_serial C)

################################################################################
# Include paths
################################################################################
include_directories(
    ${BLADERF_HOST_COMMON_INCLUDE_DIRS}
    ${libbladeRF_SOURCE_DIR}/include
)

################################################################################
# write_serial program
################################################################################
add_executable(write_serial ${CMAKE_CURRENT_SOURCE_DIR}/write_serial.c)
target_link_libraries(write_serial libbladerf_shared)

################################################################################
# Installation
################################################################################
if(NOT DEFINED BIN_INSTALL_DIR)
    set(BIN_INSTALL_DIR bin)
endif()

install(TARGETS write_serial DESTINATION ${BIN_INSTALL_DIR})
