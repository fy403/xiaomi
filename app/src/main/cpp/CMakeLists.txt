cmake_minimum_required(VERSION 3.22.1)
project("androidplayer")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_definitions(__STDC_CONSTANT_MACROS)
#add_definitions(-D__STDC_CONSTANT_MACROS)
# 启用线程支持（必须）
find_package(Threads REQUIRED)

# FFmpeg 配置

# =============================================
# Library Path Configuration
# =============================================
set(ffmpeg_lib_dir ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI})
set(3rd_party ${CMAKE_SOURCE_DIR}/3rd_party)

# =============================================
# Include Directories
# =============================================
include_directories(
        ${CMAKE_SOURCE_DIR}/include
        ${3rd_party}/ffmpeg
        ${3rd_party}/soundtouch
        ${3rd_party}/soundstretch
)
# =============================================
# SoundTouch/SoundStretch Library Configuration
# =============================================
AUX_SOURCE_DIRECTORY(${3rd_party}/soundtouch SOUND)

list(APPEND DIRSRCS ${SOUND})
add_library(soundtouch SHARED ${DIRSRCS})

# =============================================
# FFmpeg Library Configuration
# =============================================
add_library(ffmpeg SHARED IMPORTED)
add_definitions(-D__STDC_CONSTANT_MACROS)
set_target_properties(ffmpeg PROPERTIES IMPORTED_LOCATION ${ffmpeg_lib_dir}/libffmpeg.so)

# =============================================
# Android System Libraries
# =============================================
find_library(log-lib log)
#find_package(GLEW REQUIRED)
#find_package(glfw3 REQUIRED)
# =============================================
# Main Project Library
# =============================================
file(GLOB SOURCES src/*.cpp)

add_library(${CMAKE_PROJECT_NAME} SHARED
        ${SOURCES}  # 修改: 使用 ${SOURCES} 变量传递源文件列表
        )
# =============================================
# Target Link Libraries
# =============================================
target_link_libraries(${CMAKE_PROJECT_NAME}
        # Android system libraries
        android
        aaudio
        OpenSLES
        ${log-lib}
        EGL
        GLESv3
        # Project dependencies
        ffmpeg
        soundtouch
        )
