cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)  # Enable CMAKE_MSVC_RUNTIME_LIBRARY
# Force static CRT before project() so toolchain detection uses /MT not /MD
if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)
endif()
project(Tantrums CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Runtime library (match LLVM's /MT static CRT) ──────
# For clang++ (not clang-cl) on Windows, strip the auto-injected /MD CRT flags
# and replace with /MT equivalents so .obj files match the LLVM prebuilt libs.
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    foreach(_fv CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG
               CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        string(REPLACE "-D_DLL" "" ${_fv} "${${_fv}}")
        string(REPLACE "--dependent-lib=msvcrt" "--dependent-lib=libcmt" ${_fv} "${${_fv}}")
        string(REPLACE "--dependent-lib=msvcrtd" "--dependent-lib=libcmtd" ${_fv} "${${_fv}}")
    endforeach()
endif()

# ── Compiler ──────────────────────────────────────────
# Prefer Clang if available — look in common install paths per platform.
if(NOT CMAKE_CXX_COMPILER)
    if(WIN32)
        find_program(CLANGXX "clang++"
            PATHS "C:/Program Files/LLVM/bin" "C:/LLVM/bin"
            NO_DEFAULT_PATH)
    elseif(APPLE)
        find_program(CLANGXX "clang++"
            PATHS "/usr/local/opt/llvm/bin" "/opt/homebrew/opt/llvm/bin" "/usr/bin"
            NO_DEFAULT_PATH)
    else()
        find_program(CLANGXX "clang++"
            PATHS "/usr/bin" "/usr/local/bin"
            NO_DEFAULT_PATH)
    endif()
    if(CLANGXX)
        set(CMAKE_CXX_COMPILER "${CLANGXX}")
        message(STATUS "Using clang++: ${CLANGXX}")
    endif()
endif()

# ── Target triple ────────────────────────────────────
execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
                OUTPUT_VARIABLE TANTRUMS_TRIPLE
                OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "Target triple: ${TANTRUMS_TRIPLE}")

# ── LLVM ─────────────────────────────────────────────
set(LLVM_ROOT "${CMAKE_SOURCE_DIR}/external/llvm-backend")
set(LLVM_INCLUDE_DIR "${LLVM_ROOT}/include")
set(LLVM_LIB_DIR     "${LLVM_ROOT}/lib")

if(NOT EXISTS "${LLVM_INCLUDE_DIR}")
    message(FATAL_ERROR "LLVM backend not found at ${LLVM_ROOT}")
endif()

# Collect all LLVM static libs (.lib on Windows, .a on Linux/macOS)
file(GLOB ALL_LLVM_LIBS "${LLVM_LIB_DIR}/*.lib")
if(NOT ALL_LLVM_LIBS)
    file(GLOB ALL_LLVM_LIBS "${LLVM_LIB_DIR}/*.a")
endif()

# Exclude LLVMWindowsManifest (Windows-only) and lld* (invoked externally)
set(LLVM_LIBS "")
foreach(lib ${ALL_LLVM_LIBS})
    get_filename_component(libname ${lib} NAME)
    if(NOT libname MATCHES "WindowsManifest" AND NOT libname MATCHES "^lld")
        list(APPEND LLVM_LIBS ${lib})
    endif()
endforeach()

# ── Runtime bitcode embedding (Option A LTO) ─────────
# Each runtime source is compiled to LLVM bitcode (.bc).
# All .bc files are merged into one via llvm-link.
# The merged .bc is converted to a C header (const unsigned char[]).
# The compiler includes this header and links the runtime module directly
# into the user's LLVM module before optimization — enabling full LTO.

set(RUNTIME_SOURCES
    "${CMAKE_SOURCE_DIR}/src/runtime.cpp"
    "${CMAKE_SOURCE_DIR}/src/value.cpp"
    "${CMAKE_SOURCE_DIR}/src/memory.cpp"
    "${CMAKE_SOURCE_DIR}/src/table.cpp"
    "${CMAKE_SOURCE_DIR}/src/chunk.cpp"
    "${CMAKE_SOURCE_DIR}/src/stdlib/maths.cpp"
    "${CMAKE_SOURCE_DIR}/src/stdlib/filesystem.cpp")

find_program(LLVM_LINK_EXE "llvm-link"
    PATHS "${CMAKE_SOURCE_DIR}/external/llvm-backend/bin"
    NO_DEFAULT_PATH)
if(NOT LLVM_LINK_EXE)
    message(FATAL_ERROR "llvm-link not found in external/llvm-backend/bin")
endif()

# Platform-specific compile flags for bitcode compilation
set(RT_BC_FLAGS "")
if(WIN32)
    list(APPEND RT_BC_FLAGS -D_CRT_SECURE_NO_WARNINGS -D_MT)
endif()

# Compile each source to bitcode
set(RT_BCS "")
foreach(src ${RUNTIME_SOURCES})
    get_filename_component(base ${src} NAME_WE)
    set(bc "${CMAKE_BINARY_DIR}/rt_${base}.bc")
    add_custom_command(
        OUTPUT "${bc}"
        COMMAND "${CMAKE_CXX_COMPILER}"
                -std=c++23
                --target=${TANTRUMS_TRIPLE}
                -I "${CMAKE_SOURCE_DIR}/include"
                ${RT_BC_FLAGS}
                -emit-llvm -c -O2
                "${src}" -o "${bc}"
        DEPENDS "${src}"
        COMMENT "Compiling ${base}.cpp -> rt_${base}.bc (bitcode)"
        VERBATIM
    )
    list(APPEND RT_BCS "${bc}")
endforeach()

# Merge all bitcode files into one
set(MERGED_BC "${CMAKE_BINARY_DIR}/tantrums_runtime.bc")
add_custom_command(
    OUTPUT "${MERGED_BC}"
    COMMAND "${LLVM_LINK_EXE}" ${RT_BCS} -o "${MERGED_BC}"
    DEPENDS ${RT_BCS}
    COMMENT "Merging runtime bitcode -> tantrums_runtime.bc"
    VERBATIM
)

# Convert merged .bc to C header using embed_binary.cmake.

set(RUNTIME_BC_HEADER "${CMAKE_BINARY_DIR}/tantrums_runtime_bc.h")
add_custom_command(
    OUTPUT "${RUNTIME_BC_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
        -DBIN_FILE=${MERGED_BC}
        -DOUT_FILE=${RUNTIME_BC_HEADER}
        -DVARIABLE_NAME=tantrums_runtime_bc
        -P "${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake"
    DEPENDS "${MERGED_BC}"
    COMMENT "Embedding tantrums_runtime.bc -> tantrums_runtime_bc.h"
    VERBATIM
)
add_custom_target(runtime_bc_header DEPENDS "${RUNTIME_BC_HEADER}")

# ── Main executable sources ───────────────────────────
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
    src/stdlib/maths.cpp
    src/stdlib/filesystem.cpp
    src/bytecode_file.cpp
    src/runtime.cpp
)

add_executable(tantrums ${SOURCES})
add_dependencies(tantrums runtime_bc_header)

target_include_directories(tantrums PRIVATE
    include
    "${LLVM_INCLUDE_DIR}"
    "${CMAKE_BINARY_DIR}"  # for tantrums_runtime_bc.h
)

# Compile definitions — platform-conditional
target_compile_definitions(tantrums PRIVATE
    TANTRUMS_TARGET_TRIPLE="${TANTRUMS_TRIPLE}"
)
if(WIN32)
    target_compile_definitions(tantrums PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ── Link LLVM + system libs ──────────────────────────
# X86 target libs must be force-linked because their global constructors
# handle target registration — the linker would otherwise dead-strip them.
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

# /WHOLEARCHIVE is MSVC/lld-link only; --whole-archive is GNU ld / lld on Linux
foreach(x86lib ${LLVM_X86_LIBS})
    if(MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        target_link_options(tantrums PRIVATE "LINKER:/WHOLEARCHIVE:${x86lib}")
    else()
        target_link_options(tantrums PRIVATE
            "LINKER:--whole-archive" "${x86lib}" "LINKER:--no-whole-archive")
    endif()
endforeach()

if(WIN32)
    target_link_libraries(tantrums PRIVATE psapi advapi32 shell32 ole32 uuid ntdll)
elseif(APPLE)
    target_link_libraries(tantrums PRIVATE pthread dl m)
else()
    target_link_libraries(tantrums PRIVATE pthread dl m)
endif()

# Compiler options
if(MSVC)
    target_compile_options(tantrums PRIVATE /W3 /EHsc /bigobj /MT)
elseif(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # clang++ on Windows: force static CRT to match LLVM prebuilt libs (MT_StaticRelease)
    target_compile_options(tantrums PRIVATE -Wall -Wextra
        -Xclang --dependent-lib=libcmt)
    target_compile_definitions(tantrums PRIVATE _MT)
    target_link_options(tantrums PRIVATE
        "LINKER:/NODEFAULTLIB:msvcrt"
        "LINKER:/NODEFAULTLIB:msvcrtd")
else()
    target_compile_options(tantrums PRIVATE -Wall -Wextra)
endif()
