cmake_minimum_required(VERSION 3.13)
project(KampwngCompiler C)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

llvm_map_components_to_libnames(llvm_libs core executionengine analysis native target)

set(SOURCES
    src/kampwng.c
    src/lexer.c
    src/parser.c
    src/codegen.c
)

add_executable(kampwng ${SOURCES})

# Headers
target_include_directories(kampwng PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LLVM_INCLUDE_DIRS})
target_include_directories(kampwng PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(kampwng PRIVATE ${LLVM_DEFINITIONS})

# Link the specific LLVM components
# target_link_libraries(kampwng PRIVATE ${llvm_libs})

target_link_libraries(kampwng PRIVATE LLVM)

if(UNIX)
    target_link_libraries(kampwng PRIVATE 
        m        # Math library
        pthread  # Threading
        dl       # Dynamic loading
        z        # Compression
        ncurses  # Terminal handling
    )
endif()
