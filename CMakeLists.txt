cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 20)
project(p1gen VERSION 0.1.0 LANGUAGES C CXX)

find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(p1gen main.cpp)
target_link_libraries(p1gen PRIVATE Boost::program_options)

include(GNUInstallDirs)
install(TARGETS p1gen
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
