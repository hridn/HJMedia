cmake_minimum_required(VERSION 3.20)

project(HJMediaOpenGLStudyDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)

find_package(Threads REQUIRED)

function(add_opengl_study_demo target source)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../../studyDemo
    )
    target_link_libraries(${target} PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_opengl_study_demo(opengl_01_egl_surface_lifecycle 01-egl-surface-lifecycle.cpp)
add_opengl_study_demo(opengl_02_texture_shader_fbo_chain 02-texture-shader-fbo-chain.cpp)
add_opengl_study_demo(opengl_03_pbo_readback_pipeline 03-pbo-readback-pipeline.cpp)
