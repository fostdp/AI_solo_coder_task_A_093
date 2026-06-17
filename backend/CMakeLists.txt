cmake_minimum_required(VERSION 3.15)
project(seismograph_backend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(USE_BOOST_LOCKFREE "Use Boost.Lockfree queues" OFF)
option(USE_SPDLOG "Use spdlog for logging" OFF)

set(SOURCES
    src/main.cpp
    src/column_simulation.cpp
    src/sensitivity_analysis.cpp
    src/udp_server.cpp
    src/clickhouse_client.cpp
    src/mqtt_client.cpp
    src/http_server.cpp
    src/alert_manager.cpp
    src/config_loader.cpp
    src/udp_receiver.cpp
    src/seismic_simulator.cpp
    src/sensitivity_analyzer.cpp
    src/alarm_mqtt.cpp
    src/logger.cpp
    src/prometheus_metrics.cpp
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
    include/config_loader.h
    include/message_queue.h
    include/udp_receiver.h
    include/seismic_simulator.h
    include/sensitivity_analyzer.h
    include/alarm_mqtt.h
    include/logger.h
    include/prometheus_metrics.h
)

add_executable(seismograph_backend ${SOURCES} ${HEADERS})

target_include_directories(seismograph_backend PRIVATE include)

if(WIN32)
    target_link_libraries(seismograph_backend PRIVATE ws2_32)
endif()

if(USE_BOOST_LOCKFREE)
    find_package(Boost REQUIRED)
    target_include_directories(seismograph_backend PRIVATE ${Boost_INCLUDE_DIRS})
    target_compile_definitions(seismograph_backend PRIVATE SEISMOGRAPH_USE_BOOST_LOCKFREE)
endif()

if(USE_SPDLOG)
    find_package(spdlog REQUIRED)
    target_link_libraries(seismograph_backend PRIVATE spdlog::spdlog)
    target_compile_definitions(seismograph_backend PRIVATE SEISMOGRAPH_USE_SPDLOG)
endif()

target_compile_definitions(seismograph_backend PRIVATE _WIN32_WINNT=0x0601)

install(TARGETS seismograph_backend RUNTIME DESTINATION bin)

enable_testing()
