cmake_minimum_required(VERSION 3.21)

# NDK 路径：优先使用环境变量 ANDROID_NDK_HOME，否则使用默认路径
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
    set(NDK_PATH C:/Android/Sdk/ndk/27.3.13750724) # 记得修改一下ndk路径
else()
    set(NDK_PATH $ENV{ANDROID_NDK_HOME})
endif()

set(ANDROID_ABI arm64-v8a)
set(ANDROID_NDK ${NDK_PATH})
set(ANDROID_SDK_ROOT ${NDK_PATH})
set(CMAKE_TOOLCHAIN_FILE ${NDK_PATH}/build/cmake/android.toolchain.cmake)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

project(android_virtual_touch VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(touch
    src/main.cpp
    src/touch/touch.cpp
    src/touch/vector2.cpp
)

target_include_directories(touch PRIVATE include)
target_compile_features(touch PRIVATE cxx_std_20)

find_library(log-lib log PATHS ${ANDROID_NDK}/sources/android)
target_link_libraries(touch ${log-lib})
