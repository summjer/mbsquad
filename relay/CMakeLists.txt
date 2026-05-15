cmake_minimum_required(VERSION 3.16)
project(squad-relay LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# ── 生成 web_content.h ──
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    find_program(PYTHON3_BIN python3)
    if(NOT PYTHON3_BIN)
        message(FATAL_ERROR "python3 not found — needed to generate web_content.h")
    endif()
    set(PYTHON3_BIN "${PYTHON3_BIN}")
else()
    set(PYTHON3_BIN "${Python3_EXECUTABLE}")
endif()

set(GEN_WEB_SCRIPT "${CMAKE_SOURCE_DIR}/gen_web.py")
set(WEB_HTML_FILE  "${CMAKE_SOURCE_DIR}/web/ui.html")
set(WEB_HEADER     "${CMAKE_SOURCE_DIR}/src/web_content.h")

add_custom_command(
    OUTPUT  ${WEB_HEADER}
    COMMAND ${PYTHON3_BIN} ${GEN_WEB_SCRIPT}
    DEPENDS ${GEN_WEB_SCRIPT} ${WEB_HTML_FILE}
    COMMENT "Generating web_content.h from web/ui.html"
)
add_custom_target(gen_web DEPENDS ${WEB_HEADER})

# ── MinGW 交叉编译 ──
if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -O2 -s")
    add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT=0)
endif()

add_executable(squad-relay main.cpp)
add_dependencies(squad-relay gen_web)
target_include_directories(squad-relay PRIVATE vendor ${CMAKE_SOURCE_DIR})

if(WIN32 OR MINGW)
    target_link_libraries(squad-relay PRIVATE ws2_32 bcrypt)
endif()
