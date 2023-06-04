cmake_minimum_required(VERSION 3.16)

project(p1gen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -pthread -Wno-psabi -g)

find_package(Boost REQUIRED)

add_executable(p1gen)

target_sources(p1gen PRIVATE src/config.cpp)
target_sources(p1gen PRIVATE src/logf.cpp)
target_sources(p1gen PRIVATE src/www.cpp)
target_sources(p1gen PRIVATE src/settings.cpp)
target_sources(p1gen PRIVATE src/modbus.cpp)
target_sources(p1gen PRIVATE src/core.cpp)

target_sources(p1gen PRIVATE src/p1out.cpp)
target_sources(p1gen PRIVATE src/sma.cpp)
target_sources(p1gen PRIVATE src/policies.cpp)
target_sources(p1gen PRIVATE src/simulator.cpp)
target_sources(p1gen PRIVATE src/monitor.cpp)

target_link_libraries(p1gen PRIVATE pthread)

include(GNUInstallDirs)
install(TARGETS p1gen
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
