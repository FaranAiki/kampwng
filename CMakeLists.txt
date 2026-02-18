cmake_minimum_required(VERSION 3.10)
project(alkyl C)

set(CMAKE_C_STANDARD 11)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_compile_options(-Wall -Wextra -Werror -g -g0
-Wno-missing-field-initializers
)

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
    src/driver
    src/semantic
    src/alir
    src/alick
    src/common
    src/llvm_codegen
)

set(FRONTEND_SOURCES 
    src/lexer/lexer.c
    src/lexer/emitter.c    
    
    src/parser/core.c 
    src/parser/expr.c 
    src/parser/stmt.c 
    src/parser/top.c 
    src/parser/emitter.c  
    src/parser/link.c  
    
)

set(SEMANTIC_SOURCES
    src/semantic/check.c
    src/semantic/table.c
    src/semantic/emitter.c
    src/semantic/type.c
    src/semantic/core.c
)

set(ALIR_SOURCES
    src/alir/core.c 
    src/alir/generator.c
    src/alir/const.c
    src/alir/flux.c
    src/alir/utils.c
    src/alir/lvalue.c
    src/alir/emitter.c
)

set (ALICK_SOURCES

)

set(HELPER_SOURCES
    src/common/diagnostic.c
    src/common/common.c
    src/common/arena.c
    src/common/context.c
)

set(DEPRECATED_SEMANTIC_SOURCES
    src/old/semantic/semantic.c    
    src/old/semantic/check.c    
    src/old/semantic/scope.c    
    src/old/semantic/utils.c 
)

set(DEPRECATED_LLVM_CODEGEN_SOURCES
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

set_source_files_properties(${DEPRECATED_LLVM_CODEGEN_SOURCES} 
    PROPERTIES COMPILE_OPTIONS "-w"
)

set(COMMON_SOURCES
  ${FRONTEND_SOURCES}
  ${SEMANTIC_SOURCES}
  ${ALIR_SOURCES}
  ${ALICK_SOURCES}
  ${HELPER_SOURCES}
  ${DEPRECATED_LLVM_CODEGEN_SOURCES}
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
