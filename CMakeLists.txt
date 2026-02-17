cmake_minimum_required(VERSION 3.10)
project(alkyl C)

set(CMAKE_C_STANDARD 11)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(llvm_libs 
    core 
    executionengine 
    mcjit 
    interpreter 
    native 
    support
    analysis
)

include_directories(
    include 
    src/lexer 
    src/parser 
    src/codegen 
    src/driver
)

set(MUST_LLVM_SOURCES

)

set(ALIR_SOURCES
    src/alir/core.c 
    src/alir/generator.c
)

set(DEPRECATED_SOURCES
    src/old/semantic/semantic.c    
    src/old/semantic/check.c    
    src/old/semantic/scope.c    
    src/old/semantic/utils.c 

    src/old/codegen/stmt.c
    src/old/codegen/flow.c
    src/old/codegen/flux.c
    src/old/codegen/core.c
    src/old/codegen/expr.c
    src/old/codegen/builtin.c
    src/old/codegen/utils.c

    src/old/codegen/node/access.c
    src/old/codegen/node/operator.c
    src/old/codegen/node/cast.c
)

set(FRONTEND_SOURCES 
    src/lexer/lexer.c
    src/lexer/emitter.c    
    
    src/parser/core.c 
    src/parser/expr.c 
    src/parser/stmt.c 
    src/parser/top.c 
    src/parser/emitter.c  
    
)

set(HELPER_SOURCES
    src/diagnostic/diagnostic.c
)

set(COMMON_SOURCES
  ${FRONTEND_SOURCES}
  ${MUST_LLVM_SOURCES}
  ${ALIR_SOURCES}
  ${HELPER_SOURCES}
  ${DEPRECATED_SOURCES}
)

add_executable(alkyl src/driver/main.c ${COMMON_SOURCES})
add_executable(alkyl-cli src/driver/cli.c ${COMMON_SOURCES})

# Headers
target_include_directories(alkyl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LLVM_INCLUDE_DIRS})
target_include_directories(alkyl-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${LLVM_INCLUDE_DIRS})

# target_link_libraries(alkyl PRIVATE ${llvm_libs})
# target_link_libraries(alkyl-cli PRIVATE ${llvm_libs})
target_link_libraries(alkyl PRIVATE LLVM)
target_link_libraries(alkyl-cli PRIVATE LLVM)

find_package(PkgConfig)
pkg_check_modules(READLINE readline)
if(READLINE_FOUND)
    target_include_directories(alkyl PRIVATE ${READLINE_INCLUDE_DIRS})
    target_link_libraries(alkyl PRIVATE ${READLINE_LIBRARIES})
    target_compile_definitions(alkyl PRIVATE HAVE_READLINE)
endif()

if(UNIX)
    target_link_libraries(alkyl PRIVATE m pthread dl z ncurses readline)
    target_link_libraries(alkyl-cli PRIVATE m pthread dl z ncurses readline)
endif()
