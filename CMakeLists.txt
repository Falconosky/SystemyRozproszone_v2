cmake_minimum_required(VERSION 3.10)
project(connection_manager)

set(CMAKE_CXX_STANDARD 11)

# Znajdź pakiet GTK+
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# Ustawienia kompilatora
include_directories(${GTK3_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -rdynamic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -rdynamic")

# Plik źródłowy
add_executable(connection_manager main.cpp
        connection_manager/connection_manager.cpp
        connection_manager/connection_manager.h
        client/client.cpp
        client/client.h
        main.h
        checkDebugConfig.cpp
        checkDebugConfig.h
        client/client_threads.cpp
        client/client_threads.h
)

# Linkowanie GTK+
target_link_libraries(connection_manager ${GTK3_LIBRARIES})