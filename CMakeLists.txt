cmake_minimum_required(VERSION 3.16)

project(p1gen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -pthread)

find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(p1gen main.cpp)
target_link_libraries(p1gen PRIVATE Boost::program_options pthread)

include(GNUInstallDirs)
install(TARGETS p1gen
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
