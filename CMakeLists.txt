cmake_minimum_required(VERSION 3.22)

project(silent LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Настройки для Dobby
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)

# Подключаем Dobby
add_subdirectory(so2_backtrack/third_party/Dobby dobby_build)

# Сборка вашей библиотеки
add_library(silent SHARED src/silent.cpp)

# Указываем пути к заголовкам вашего проекта и внутренним папкам Dobby,
# чтобы компилятор без проблем находил все файлы вроде Cpu.h
target_include_directories(silent PRIVATE 
    src 
    so2_backtrack/third_party/Dobby/include
    so2_backtrack/third_party/Dobby/source
    so2_backtrack/third_party/Dobby/source/core/arch
    so2_backtrack/third_party/Dobby/source/core/arch/arm
)

target_compile_options(silent PRIVATE -Wall -Wextra -fvisibility=hidden)

# Линкуем статическую библиотеку Dobby и системный лог Android
target_link_libraries(silent PRIVATE dobby_static log)
