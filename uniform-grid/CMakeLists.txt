add_library(emper-module-uniform-grid
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UniformGrid.cpp
)

target_include_directories(emper-module-uniform-grid PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(emper-module-uniform-grid PUBLIC
    emper-engine
)