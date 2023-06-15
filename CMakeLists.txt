cmake_minimum_required(VERSION 3.16)

project(p1faker VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -pthread -Wno-psabi -g)

find_package(Boost REQUIRED)

add_executable(p1faker)

target_sources(p1faker PRIVATE src/config.cpp)
target_sources(p1faker PRIVATE src/logf.cpp)
target_sources(p1faker PRIVATE src/service_discovery.cpp)
target_sources(p1faker PRIVATE src/www.cpp)
target_sources(p1faker PRIVATE src/settings.cpp)
target_sources(p1faker PRIVATE src/core.cpp)
target_sources(p1faker PRIVATE src/modbus.cpp)

target_sources(p1faker PRIVATE src/p1out.cpp)
target_sources(p1faker PRIVATE src/sma.cpp)
target_sources(p1faker PRIVATE src/policies.cpp)
target_sources(p1faker PRIVATE src/simulator.cpp)
target_sources(p1faker PRIVATE src/monitor.cpp)
target_sources(p1faker PRIVATE src/schedule.cpp)

target_link_libraries(p1faker PRIVATE pthread)
target_link_libraries(p1faker PRIVATE avahi-client avahi-common)

include(GNUInstallDirs)
install(TARGETS p1faker
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
