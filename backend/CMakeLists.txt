cmake_minimum_required(VERSION 3.15)
project(seismograph_backend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(SOURCES
    src/main.cpp
    src/column_simulation.cpp
    src/sensitivity_analysis.cpp
    src/udp_server.cpp
    src/clickhouse_client.cpp
    src/mqtt_client.cpp
    src/http_server.cpp
    src/alert_manager.cpp
)

set(HEADERS
    include/common.h
    include/column_simulation.h
    include/sensitivity_analysis.h
    include/udp_server.h
    include/clickhouse_client.h
    include/mqtt_client.h
    include/http_server.h
    include/alert_manager.h
)

add_executable(seismograph_backend ${SOURCES} ${HEADERS})

target_include_directories(seismograph_backend PRIVATE include)

if(WIN32)
    target_link_libraries(seismograph_backend PRIVATE ws2_32)
endif()

target_compile_definitions(seismograph_backend PRIVATE _WIN32_WINNT=0x0601)

install(TARGETS seismograph_backend RUNTIME DESTINATION bin)

enable_testing()
