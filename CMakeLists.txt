add_library(emper-module-uniform-grid
    uniform-grid/UniformGrid.cpp
)

target_include_directories(emper-module-uniform-grid PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/uniform-grid
)

target_link_libraries(emper-module-uniform-grid PUBLIC
    emper-engine
)

find_package(OpenMP REQUIRED)

add_library(emper-module-flock
    flock/Flock.cpp
)

target_include_directories(emper-module-flock PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/flock
)

target_link_libraries(emper-module-flock PUBLIC
    emper-engine
    emper-module-uniform-grid
    OpenMP::OpenMP_CXX
)
