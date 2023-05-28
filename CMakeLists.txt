cmake_minimum_required(VERSION 3.16)

project(p1gen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -pthread -Wno-psabi -g)

find_package(Boost REQUIRED)

add_executable(p1gen config.cpp logf.cpp settings.cpp www.cpp core.cpp)
target_sources(p1gen PRIVATE p1out.cpp sma.cpp policies.cpp)
target_link_libraries(p1gen PRIVATE pthread)

include(GNUInstallDirs)
install(TARGETS p1gen
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
