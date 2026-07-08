link_directories("${CMAKE_CURRENT_LIST_DIR}")

set(QR_EXTRA_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/qr_detector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/qr_result_store.cpp
    ${CMAKE_CURRENT_LIST_DIR}/qr_worker.cpp
)

# 方式 1：把 quirc 源码放到 main/third_party/quirc
# 需要至少包含：lib/quirc.c lib/decode.c lib/identify.c lib/version_db.c
# 不同 quirc 版本文件名可能略有差异，按你的实际目录增删。
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/third_party/quirc/lib/quirc.c")
    list(APPEND QR_EXTRA_SRCS
        ${CMAKE_CURRENT_LIST_DIR}/third_party/quirc/lib/quirc.c
        ${CMAKE_CURRENT_LIST_DIR}/third_party/quirc/lib/decode.c
        ${CMAKE_CURRENT_LIST_DIR}/third_party/quirc/lib/identify.c
        ${CMAKE_CURRENT_LIST_DIR}/third_party/quirc/lib/version_db.c
    )
    set(QUIRC_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/quirc/lib")
else()
    # 方式 2：系统或 SDK 已经提供 quirc.h/libquirc 时使用。
    set(QUIRC_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()

component_register(
    COMPONENT_NAME main

    SRCS
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/http_server.cpp
    ${CMAKE_CURRENT_LIST_DIR}/rtsp_demo.cpp
    ${CMAKE_CURRENT_LIST_DIR}/frame_sei.cpp
    ${CMAKE_CURRENT_LIST_DIR}/frame_sync.cpp
    ${QR_EXTRA_SRCS}

    PRIVATE_REQUIREDS
    cvi_rtsp
    sophgo

    INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}"
    "${QUIRC_INCLUDE_DIR}"
)
