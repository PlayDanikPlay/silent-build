cmake_minimum_required(VERSION 3.22)

project(silent LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Создаем простую разделяемую библиотеку libsilent.so
add_library(silent SHARED src/silent.cpp)

# Настройки путей и компиляции
target_include_directories(silent PRIVATE src)
target_compile_options(silent PRIVATE -Wall -Wextra -fvisibility=hidden)

# Указываем системные библиотеки Android, если они нужны (например, логирование)
target_link_libraries(silent PRIVATE log)
