cmake_minimum_required(VERSION 3.20)

project(HJMediaOpenGLThreeDayStudy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

function(add_opengl_study_demo target source)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../studyDemo
    )
    target_link_libraries(${target} PRIVATE Threads::Threads)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_opengl_study_demo(day01_egl_first_frame day01_egl_first_frame.cpp)
add_opengl_study_demo(day02_fbo_invert_chain day02_fbo_invert_chain.cpp)
add_opengl_study_demo(day03_double_pbo_readback day03_double_pbo_readback.cpp)
