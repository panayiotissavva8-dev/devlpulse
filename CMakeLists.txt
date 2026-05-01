cmake_minimum_required(VERSION 3.20)
project(devpulse VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── System deps ───────────────────────────────────────────────
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

# ── FetchContent ───────────────────────────────────────────────
include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.2.1
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.10.5
)

FetchContent_MakeAvailable(crow nlohmann_json cpr)

# ── Normalize Crow target (Crow is inconsistent, because why not) ──
set(CROW_LIB "")
if(TARGET Crow::Crow)
  set(CROW_LIB Crow::Crow)
elseif(TARGET crow)
  set(CROW_LIB crow)
elseif(TARGET Crow)
  set(CROW_LIB Crow)
else()
  message(FATAL_ERROR "Crow target not found after FetchContent")
endif()

# ── Executable ───────────────────────────────────────────────
add_executable(devpulse
  src/main.cpp
)

target_include_directories(devpulse PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(devpulse PRIVATE
  ASIO_STANDALONE
  CROW_ENABLE_SSL
)

target_link_libraries(devpulse PRIVATE
  ${CROW_LIB}
  nlohmann_json::nlohmann_json
  cpr::cpr
  SQLite::SQLite3
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(devpulse PRIVATE -O3 -DNDEBUG)
endif()

install(TARGETS devpulse DESTINATION bin)