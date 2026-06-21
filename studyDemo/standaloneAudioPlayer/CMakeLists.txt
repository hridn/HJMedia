cmake_minimum_required(VERSION 3.20)

project(StandaloneAudioPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(standalone_audio_player
        src/main.cpp
)

target_compile_definitions(standalone_audio_player PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(WIN32)
    target_link_libraries(standalone_audio_player PRIVATE winmm)
else()
    message(FATAL_ERROR "standalone_audio_player currently targets Windows waveOut only.")
endif()

if(MSVC)
    target_compile_options(standalone_audio_player PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(standalone_audio_player PRIVATE -Wall -Wextra -Wpedantic)
endif()
