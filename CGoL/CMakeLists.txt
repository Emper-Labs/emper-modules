find_package(OpenMP REQUIRED)

add_library(emper-module-CGoL
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CGoL.cpp
)

target_include_directories(emper-module-CGoL PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(emper-module-CGoL PUBLIC
    emper-engine
    emper-backend-opengl
    OpenMP::OpenMP_CXX
)
