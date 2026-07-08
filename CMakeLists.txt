cmake_minimum_required(VERSION 3.5.0)

project(qrcode_rec C CXX)

include_directories("/home/yylin/sg2002_recamera_emmc/osdrv/interdrv/include/chip/cv181x/uapi/")
include_directories("/home/yylin/sg2002_recamera_emmc/osdrv/interdrv/include/common/uapi/")
include_directories("/home/yylin/sg2002_recamera_emmc/osdrv/interdrv/include/common/uapi/linux/")


include_directories("/home/yylin/sg2002_recamera_emmc/cvi_mpi/include")
include_directories("/home/yylin/sg2002_recamera_emmc/cvi_mpi/modules/isp/cv181x/isp-daemon2/inc/")
include_directories("/home/yylin/sg2002_recamera_emmc/cvi_mpi/modules/isp/common/raw_dump/inc")
include_directories("/home/yylin/sg2002_recamera_emmc/cvi_mpi/sample/")
include_directories("/home/yylin/sg2002_recamera_emmc/cvi_mpi/modules/isp/include/cv181x/")

get_filename_component(PROJECT_DIR ${CMAKE_CURRENT_LIST_DIR} ABSOLUTE)
message(STATUS "PROJECT_DIR: ${PROJECT_DIR}")

get_filename_component(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)
message(STATUS "ROOT_DIR: ${ROOT_DIR}")

include(${ROOT_DIR}/cmake/toolchain-riscv64-linux-musl-x86_64.cmake)
include(${ROOT_DIR}/cmake/project.cmake)

