cmake_minimum_required(VERSION 3.22)

project(silent LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Настройки для автоматического подключения Dobby, который скачает скрипт сборки
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)

add_subdirectory(so2_backtrack/third_party/Dobby dobby_build)

# Сборка вашей библиотеки
add_library(silent SHARED src/silent.cpp)

target_include_directories(silent PRIVATE 
    src 
    so2_backtrack/third_party/Dobby/include
)

target_compile_options(silent PRIVATE -Wall -Wextra -fvisibility=hidden)
target_link_libraries(silent PRIVATE dobby_static log)
