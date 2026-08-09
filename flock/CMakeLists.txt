find_package(OpenMP REQUIRED)

add_library(emper-module-flock
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Flock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CpuCompute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/GpuCompute.cpp
)

target_include_directories(emper-module-flock PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(emper-module-flock PUBLIC
    emper-engine
    emper-module-uniform-grid
    OpenMP::OpenMP_CXX
)
