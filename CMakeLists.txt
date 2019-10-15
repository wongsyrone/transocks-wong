cmake_minimum_required(VERSION 3.7.2)
project(transocks-wong)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

include(TrMacros)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
add_definitions(-std=c11 -Wno-unused-parameter -D_GNU_SOURCE)
add_executable(transocks-wong
        src/transocks.c src/util.c src/util.h src/socks5.h src/log.c src/log.h src/context.c src/context.h src/listener-tcp.c src/listener-tcp.h src/signal.c src/signal.h src/socks5.c src/pump.c src/pump.h src/pump-bufferpump.c src/pump-bufferpump.h src/pump-splicepump.c src/pump-splicepump.h src/list.h src/mem-allocator.h src/transparent-method.c src/transparent-method.h src/netutils.c src/netutils.h src/transparent-method-redirect.c src/transparent-method-redirect.h src/transparent-method-tproxy.c src/transparent-method-tproxy.h src/listener-udp.c src/listener-udp.h src/uthash.h src/uthash-wrapper.h src/uthash-guard.h src/lrucache.c src/lrucache.h)
find_package(Libevent2 REQUIRED)
include_directories(${LIBEVENT2_INCLUDE_DIR})
target_link_libraries(transocks-wong ${LIBEVENT2_LIBRARIES})

find_library(LIBRT rt)
if(LIBRT)
    target_link_libraries(transocks-wong ${LIBRT})
endif()

option(ENABLE_DEBUG "Build as DEBUG mode" OFF)
if(ENABLE_DEBUG)
    message(STATUS "DEBUG mode enabled")
    add_definitions(-DTRANSOCKS_DEBUG)
else()
    message(STATUS "DEBUG mode disabled")
endif()

tr_list_option(WITH_MEM_ALLOCATOR "Use specified memory allocator" AUTO system mimalloc)
if (WITH_MEM_ALLOCATOR STREQUAL "mimalloc")
    message(STATUS "using memory allocator: mimalloc")
    add_definitions(-DTRANSOCKS_ALLOCATOR_USE_MIMALLOC)
    find_package(mimalloc 1.0 REQUIRED)
    target_link_libraries(transocks-wong mimalloc-static)
elseif(WITH_MEM_ALLOCATOR STREQUAL "AUTO" OR WITH_MEM_ALLOCATOR STREQUAL "system")
    message(STATUS "using memory allocator: system")
    add_definitions(-DTRANSOCKS_ALLOCATOR_USE_SYSTEM)
endif()


include(GNUInstallDirs)
install(TARGETS transocks-wong DESTINATION ${CMAKE_INSTALL_BINDIR})
