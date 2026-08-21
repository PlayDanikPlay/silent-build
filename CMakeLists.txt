cmake_minimum_required(VERSION 3.22)

project(silent LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Принудительно добавляем пути к заголовкам Dobby глобально для всех целей
add_compile_options(
    -I${CMAKE_CURRENT_SOURCE_DIR}/so2_backtrack/third_party/Dobby/include
    -I${CMAKE_CURRENT_SOURCE_DIR}/so2_backtrack/third_party/Dobby/source
    -I${CMAKE_CURRENT_SOURCE_DIR}/so2_backtrack/third_party/Dobby/source/core/arch/arm
)

# Настройки для Dobby
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
