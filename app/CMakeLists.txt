qt_add_executable(utsushi
    main.cpp
    main_window.hpp main_window.cpp
)
target_link_libraries(utsushi PRIVATE utsushi_core Qt6::Widgets)
