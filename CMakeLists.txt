add_library(emper-module-uniform-grid
    uniform-grid/UniformGrid.cpp
)

target_include_directories(emper-module-uniform-grid PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/uniform-grid
)

target_link_libraries(emper-module-uniform-grid PUBLIC
    emper-engine
)
