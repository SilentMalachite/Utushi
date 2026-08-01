cmake_minimum_required(VERSION 3.21)
project(utsushi LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UTSUSHI_WERROR "Treat warnings as errors" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Pdf Test)
qt_standard_project_setup()

if(UTSUSHI_WERROR)
    if(MSVC)
        add_compile_options(/W4 /WX)
    else()
        add_compile_options(-Wall -Wextra -Werror)
    endif()
endif()

enable_testing()
add_subdirectory(app)
add_subdirectory(tests)
