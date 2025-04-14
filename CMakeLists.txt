cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 17)
set(NDK_PATH D:/android-ndk-r26b) # 记得修改一下ndk路径

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