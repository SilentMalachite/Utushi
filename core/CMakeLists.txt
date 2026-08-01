qt_add_library(utsushi_core STATIC
    page_range.hpp page_range.cpp
    render_size.hpp render_size.cpp
    output_path.hpp output_path.cpp
)
target_link_libraries(utsushi_core PUBLIC Qt6::Core Qt6::Gui Qt6::Pdf)
target_include_directories(utsushi_core PUBLIC ${CMAKE_SOURCE_DIR})
