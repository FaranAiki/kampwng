cmake_minimum_required(VERSION 3.13)
project(AlkylCompiler C)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

llvm_map_components_to_libnames(llvm_libs core executionengine analysis native target mcjit)

# --- READLINE SUPPORT ---
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(READLINE readline)
endif()

# If pkg-config failed or not found, try simple check
if(NOT READLINE_FOUND)
    find_library(READLINE_LIB readline)
    if(READLINE_LIB)
        set(READLINE_FOUND TRUE)
        set(READLINE_LIBRARIES readline)
        set(READLINE_INCLUDE_DIRS "")
    endif()
endif()

if(READLINE_FOUND)
    message(STATUS "Found Readline: ${READLINE_LIBRARIES}")
    add_definitions(-DHAVE_READLINE)
else()
    message(WARNING "Readline not found. Arrow keys will not work in CLI.")
endif()

set(COMMON_SOURCES
    src/lexer.c
    src/parser.c
    src/codegen.c
)

# Compiler Binary
add_executable(alkyl src/alkyl.c ${COMMON_SOURCES})

# REPL Binary (JIT)
add_executable(alkyl-cli src/cli.c ${COMMON_SOURCES})

# Headers
target_include_directories(alkyl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LLVM_INCLUDE_DIRS})
target_include_directories(alkyl-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LLVM_INCLUDE_DIRS})

target_compile_definitions(alkyl PRIVATE ${LLVM_DEFINITIONS})
target_compile_definitions(alkyl-cli PRIVATE ${LLVM_DEFINITIONS})

# Link Libraries
target_link_libraries(alkyl PRIVATE LLVM)
target_link_libraries(alkyl-cli PRIVATE LLVM)

if(READLINE_FOUND)
    target_link_libraries(alkyl-cli PRIVATE ${READLINE_LIBRARIES})
    target_include_directories(alkyl-cli PRIVATE ${READLINE_INCLUDE_DIRS})
endif()

if(UNIX)
    target_link_libraries(alkyl PRIVATE m pthread dl z ncurses)
    target_link_libraries(alkyl-cli PRIVATE m pthread dl z ncurses)
endif()
