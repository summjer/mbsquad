cmake_minimum_required(VERSION 3.10)
project(squad-panel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

# Source files — explicit listing for build reproducibility
set(SOURCES
    src/main.cpp
    src/core/auth.cpp
    src/core/core.cpp
    src/core/database.cpp
    src/core/local_log_tailer.cpp
    src/core/log_parser.cpp
    src/core/points_service.cpp
    src/core/remote_api_poller.cpp
    src/handlers/admin_handler.cpp
    src/handlers/auth_handler.cpp
    src/handlers/ban_handler.cpp
    src/handlers/chat_handler.cpp
    src/handlers/config_handler.cpp
    src/handlers/event_handler.cpp
    src/handlers/kill_handler.cpp
    src/handlers/misc_handler.cpp
    src/handlers/missing_routes.cpp
    src/handlers/relay_routes.cpp
    src/handlers/analytics_routes.cpp
    src/handlers/extras_routes.cpp
    src/handlers/player_handler.cpp
    src/handlers/plugin_handler.cpp
    src/handlers/points_handler.cpp
    src/handlers/query_handler.cpp
    src/handlers/rcon_handler.cpp
    src/handlers/server_handler.cpp
    src/handlers/tk_handler.cpp
    src/handlers/team_handler.cpp
    src/net/rcon.cpp
    src/net/server.cpp
    src/net/ws_handler.cpp
    src/plugins/cdk_redeem_plugin.cpp
    src/plugins/chat_commands.cpp
    src/plugins/plugin_manager.cpp
    src/plugins/switch_lock_plugin.cpp
    src/plugins/team_balance_plugin.cpp
    src/plugins/tk_forgive_plugin.cpp
    src/plugins/welcome_plugin.cpp
    src/tasks/tasks.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Threads::Threads
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    dl
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
target_compile_definitions(${PROJECT_NAME} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${PROJECT_NAME} PRIVATE -O2 -DNDEBUG)
endif()
