cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)  # Enable CMAKE_MSVC_RUNTIME_LIBRARY
project(Tantrums CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")  # /MT to match LLVM static libs

# ── Compiler ──────────────────────────────────────────
# Use LLVM/Clang if available for consistent ABI
find_program(CLANGXX "clang++" PATHS "C:/Program Files/LLVM/bin" NO_DEFAULT_PATH)
if(CLANGXX)
    set(CMAKE_CXX_COMPILER "${CLANGXX}")
    message(STATUS "Using clang++: ${CLANGXX}")
endif()

# ── Target triple ────────────────────────────────────
if(WIN32)
    set(TANTRUMS_TRIPLE "x86_64-pc-windows-msvc")
else()
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
                    OUTPUT_VARIABLE TANTRUMS_TRIPLE
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
message(STATUS "Target triple: ${TANTRUMS_TRIPLE}")

# ── LLVM ─────────────────────────────────────────────
set(LLVM_ROOT "${CMAKE_SOURCE_DIR}/external/llvm-backend")
set(LLVM_INCLUDE_DIR "${LLVM_ROOT}/include")
set(LLVM_LIB_DIR     "${LLVM_ROOT}/lib")

if(NOT EXISTS "${LLVM_INCLUDE_DIR}")
    message(FATAL_ERROR "LLVM backend not found at ${LLVM_ROOT}")
endif()

# Collect all LLVM static libs, excluding ones with unresolvable deps
file(GLOB ALL_LLVM_LIBS "${LLVM_LIB_DIR}/*.lib")
if(NOT ALL_LLVM_LIBS)
    file(GLOB ALL_LLVM_LIBS "${LLVM_LIB_DIR}/*.a")
endif()

# Exclude LLVMWindowsManifest and lld* (we invoke lld-link externally)
set(LLVM_LIBS "")
foreach(lib ${ALL_LLVM_LIBS})
    get_filename_component(libname ${lib} NAME)
    if(NOT libname MATCHES "WindowsManifest" AND NOT libname MATCHES "^lld")
        list(APPEND LLVM_LIBS ${lib})
    endif()
endforeach()

# ── Runtime static library: tantrums_rt.lib ──────────
# Contains all runtime + value/memory/table support linked into user scripts.
set(RUNTIME_SOURCES
    "${CMAKE_SOURCE_DIR}/src/runtime.cpp"
    "${CMAKE_SOURCE_DIR}/src/value.cpp"
    "${CMAKE_SOURCE_DIR}/src/memory.cpp"
    "${CMAKE_SOURCE_DIR}/src/table.cpp"
    "${CMAKE_SOURCE_DIR}/src/chunk.cpp"
<<<<<<< HEAD
    "${CMAKE_SOURCE_DIR}/src/stdlib/maths.cpp"
=======
>>>>>>> edf689ec3e6372e0694493081bf10302d2b11174
)
set(RUNTIME_LIB "${CMAKE_BINARY_DIR}/tantrums_rt.lib")

# Compile each source to .obj
set(RT_OBJS "")
foreach(src ${RUNTIME_SOURCES})
    get_filename_component(base ${src} NAME_WE)
    set(obj "${CMAKE_BINARY_DIR}/rt_${base}.obj")
    add_custom_command(
        OUTPUT "${obj}"
        COMMAND "${CMAKE_CXX_COMPILER}"
                -std=c++23
                --target=${TANTRUMS_TRIPLE}
                -I "${CMAKE_SOURCE_DIR}/include"
                -D_CRT_SECURE_NO_WARNINGS
                -D_MT
                -Xclang --dependent-lib=libcmt
                -O2
                -c "${src}"
                -o "${obj}"
        DEPENDS "${src}"
        COMMENT "Compiling ${base}.cpp -> rt_${base}.obj"
        VERBATIM
    )
    list(APPEND RT_OBJS "${obj}")
endforeach()

# Archive into static library
add_custom_command(
    OUTPUT "${RUNTIME_LIB}"
    COMMAND llvm-lib ${RT_OBJS} /OUT:${RUNTIME_LIB}
    DEPENDS ${RT_OBJS}
    COMMENT "Archiving tantrums_rt.lib"
    VERBATIM
)
add_custom_target(runtime_lib DEPENDS "${RUNTIME_LIB}")

# ── Main executable sources (exclude vm.cpp and runtime.cpp) ──
set(SOURCES
    src/ast.cpp
    src/chunk.cpp
    src/compiler.cpp
    src/lexer.cpp
    src/LLVMCodegen.cpp
    src/main.cpp
    src/memory.cpp
    src/parser.cpp
    src/table.cpp
    src/token.cpp
    src/value.cpp
    src/bytecode_file.cpp
)

add_executable(tantrums ${SOURCES})
add_dependencies(tantrums runtime_lib)

target_include_directories(tantrums PRIVATE
    include
    "${LLVM_INCLUDE_DIR}"
)

# Compile definitions
target_compile_definitions(tantrums PRIVATE
    _CRT_SECURE_NO_WARNINGS
    TANTRUMS_RUNTIME_OBJ="${RUNTIME_LIB}"
    TANTRUMS_TARGET_TRIPLE="${TANTRUMS_TRIPLE}"
)

# ── Link LLVM + system libs ──────────────────────────
# X86 target libs must be force-linked (/WHOLEARCHIVE) because their
# global constructors handle target registration. The linker would
# otherwise dead strip them since there are no direct symbol references.
set(LLVM_X86_LIBS "")
set(LLVM_OTHER_LIBS "")
foreach(lib ${LLVM_LIBS})
    get_filename_component(libname ${lib} NAME)
    if(libname MATCHES "^LLVMX86")
        list(APPEND LLVM_X86_LIBS ${lib})
    else()
        list(APPEND LLVM_OTHER_LIBS ${lib})
    endif()
endforeach()

target_link_libraries(tantrums PRIVATE ${LLVM_OTHER_LIBS})
foreach(x86lib ${LLVM_X86_LIBS})
    target_link_options(tantrums PRIVATE "LINKER:/WHOLEARCHIVE:${x86lib}")
endforeach()

if(WIN32)
    target_link_libraries(tantrums PRIVATE psapi advapi32 shell32 ole32 uuid ntdll)
else()
    target_link_libraries(tantrums PRIVATE pthread dl m)
endif()

# Compiler options
if(MSVC)
    target_compile_options(tantrums PRIVATE /W3 /EHsc /bigobj)
else()
    target_compile_options(tantrums PRIVATE -Wall -Wextra)
endif()
