cmake_minimum_required(VERSION 3.22)

project(silent LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Отключаем всё лишнее в Dobby, чтобы не тянуть ненужные зависимости
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)

# Подключаем Dobby как подпапку
add_subdirectory(so2_backtrack/third_party/Dobby dobby_build)

# Явно исправляем проблему отсутствия Cpu.h для цели dobby_static, 
# принудительно добавляя внутреннюю папку source в пути компиляции Dobby
target_include_directories(dobby_static PRIVATE
    so2_backtrack/third_party/Dobby/source
    so2_backtrack/third_party/Dobby/source/core/arch
)

# Сборка вашей библиотеки silent
add_library(silent SHARED src/silent.cpp)

target_include_directories(silent PRIVATE 
    src 
    so2_backtrack/third_party/Dobby/include
)

target_compile_options(silent PRIVATE -Wall -Wextra -fvisibility=hidden)

# Линкуем Dobby и системный лог
target_link_libraries(silent PRIVATE dobby_static log)
