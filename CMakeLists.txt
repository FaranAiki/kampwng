cmake_minimum_required(VERSION 3.13)
project(AlkylCompiler C)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

llvm_map_components_to_libnames(llvm_libs core executionengine analysis native target)

set(SOURCES
    src/alkyl.c
    src/lexer.c
    src/parser.c
    src/codegen.c
)

add_executable(alkyl ${SOURCES})

# Headers
target_include_directories(alkyl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LLVM_INCLUDE_DIRS})
target_include_directories(alkyl PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(alkyl PRIVATE ${LLVM_DEFINITIONS})

# Link the specific LLVM components
# target_link_libraries(alkyl PRIVATE ${llvm_libs})

target_link_libraries(alkyl PRIVATE LLVM)

if(UNIX)
    target_link_libraries(alkyl PRIVATE 
        m        # Math library
        pthread  # Threading
        dl       # Dynamic loading
        z        # Compression
        ncurses  # Terminal handling
    )
endif()
