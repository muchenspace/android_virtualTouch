cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# NDK 路径：优先使用环境变量 ANDROID_NDK_HOME，否则使用默认路径
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
    set(NDK_PATH D:/android-ndk-r27c) # 记得修改一下ndk路径
else()
    set(NDK_PATH $ENV{ANDROID_NDK_HOME})
endif()

set(ANDROID_ABI arm64-v8a)
set(ANDROID_NDK ${NDK_PATH})
set(CMAKE_TOOLCHAIN_FILE ${NDK_PATH}/build/cmake/android.toolchain.cmake)
set(ANDROID_SDK_ROOT ${NDK_PATH})

project(android_virtualTouch-a)

include_directories(
        ./include
        ./include/tools
)

file(GLOB_RECURSE src "./src/*.cpp")

add_executable(touch ${src})

find_library(log-lib log PATHS ${ANDROID_NDK}/sources/android)

target_link_libraries(touch ${log-lib})