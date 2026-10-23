# 3.28 because of SafetyHook
cmake_minimum_required(VERSION 3.28)

project(SuperBLT CXX C)

###############################################################################
## misc. definitions ##########################################################
###############################################################################

if(MSVC)
	enable_language(ASM_MASM)
else()
	enable_language(ASM)
endif()

# Use C 11, no gnu ext
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Use C++ 23, no gnu ext
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# find python3
find_package(Python3 REQUIRED COMPONENTS Interpreter)

###############################################################################
## file globbing ##############################################################
###############################################################################

# these instructions search the directory tree when cmake is
# invoked and put all files that match the pattern in the variable
# `sources`
file(GLOB_RECURSE	sources		src/*.cpp src/*.c src/*.h)

###############################################################################
## target definitions #########################################################
###############################################################################

set(mxml_sources mxml-attr.c mxml-entity.c mxml-file.c mxml-get.c mxml-index.c
	mxml-node.c mxml-search.c mxml-set.c mxml-private.c mxml-string.c)
list(TRANSFORM mxml_sources PREPEND lib/mxml/)
add_library(mxml STATIC ${mxml_sources})
target_include_directories(mxml PRIVATE lib/configs/mxml)
target_compile_options(mxml PRIVATE -D_CRT_SECURE_NO_WARNINGS)
if(NOT MSVC)
	target_compile_options(mxml PRIVATE -include io.h)
endif()
target_include_directories(mxml PUBLIC lib/mxml)

file(GLOB_RECURSE wren_sources
	# Public source file
	lib/wren/src/include/wren.h
	# Core VM files
	lib/wren/src/vm/*.c
	lib/wren/src/vm/*.h
	# Optional components (Meta and Random)
	lib/wren/src/optional/*.c
	lib/wren/src/optional/*.h
)
add_library(wren STATIC ${wren_sources})
target_include_directories(wren PRIVATE lib/wren/src/optional lib/wren/src/vm)
target_compile_options(wren PRIVATE -D_CRT_SECURE_NO_WARNINGS)
target_include_directories(wren PUBLIC lib/wren/src/include)

# safetyhook pulls Zydis through CPM, point it at the bundled submodule
set(CPM_Zydis_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/lib/zydis" CACHE PATH "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/safetyhook EXCLUDE_FROM_ALL)

###############################################################################
## Wren source generation #####################################################
###############################################################################

set(wren_src_files
	wren/native.wren
	wren/DB_001.wren
	wren/internal_001.wren
	wren/LuaInterface_001.wren
	wren/Environment_001.wren
	wren/Utils_001.wren
)
add_custom_command(
	OUTPUT wren_generated_src.c gen/wren_generated_src.h
	COMMAND ${Python3_EXECUTABLE} wren/generate.py ${CMAKE_CURRENT_BINARY_DIR} ${wren_src_files}
	MAIN_DEPENDENCY wren/generate.py
	DEPENDS ${wren_src_files}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	VERBATIM
)
list(APPEND sources ${CMAKE_CURRENT_BINARY_DIR}/wren_generated_src.c)

###############################################################################
## Base (non-platform-specific) part of SuperBLT ##############################
###############################################################################

add_library(SuperBLT STATIC ${sources})

target_compile_options(SuperBLT PRIVATE
	-DCURL_STATICLIB
)
if(NOT MSVC)
	# the signature code casts between function and data pointers (MSVC allows it)
	target_compile_options(SuperBLT PRIVATE -fpermissive)
endif()

# General optimisation breaks calls to certain lua functions, so replace it.
# We statically link to reduce dependencies
foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
	if(${flag_var} MATCHES "/O2")
		string(REGEX REPLACE "/O2" "/Od /Oi /Ot /Oy /Ob2 /GF /Gy" ${flag_var} "${${flag_var}}")
	endif(${flag_var} MATCHES "/O2")
endforeach(flag_var)

# Link against libraries
if(MSVC)
	target_link_libraries(SuperBLT crypt32.lib ws2_32.lib version.lib)
else()
	target_link_libraries(SuperBLT crypt32 ws2_32 version)
endif()

# Allow both SuperBLT and the loaders access to the headers
target_include_directories(SuperBLT PUBLIC src)

if(NOT MSVC)
	# no mingw runtime DLLs next to the game, link them statically
	set(CMAKE_SHARED_LIBRARY_PREFIX "")
	target_link_options(SuperBLT PUBLIC -static -static-libgcc -static-libstdc++)
	# drop unused code and strip debug info (it makes up most of the DLL size) in release builds
	target_compile_options(SuperBLT PRIVATE -ffunction-sections -fdata-sections)
	target_link_options(SuperBLT PUBLIC -Wl,--gc-sections $<$<CONFIG:Release,MinSizeRel>:-s>)
endif()

# Make the generated wren header available
target_include_directories(SuperBLT PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)

###############################################################################
## dependencies ###############################################################
###############################################################################

target_link_libraries(SuperBLT mxml)
target_link_libraries(SuperBLT wren)
target_link_libraries(SuperBLT safetyhook)

# Build libs as static
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libs as static, set by SBLT's buildscript")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)



#### libpsl ####

set(LIBPSL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/sys/libpsl")

# Read version from version.txt
file(READ "${LIBPSL_DIR}/version.txt" LIBPSL_VERSION)
string(STRIP "${LIBPSL_VERSION}" LIBPSL_VERSION)

# Split version into major, minor, patch
string(REPLACE "." ";" LIBPSL_VERSION_LIST "${LIBPSL_VERSION}")
list(GET LIBPSL_VERSION_LIST 0 LIBPSL_VERSION_MAJOR)
list(GET LIBPSL_VERSION_LIST 1 LIBPSL_VERSION_MINOR)
list(GET LIBPSL_VERSION_LIST 2 LIBPSL_VERSION_PATCH)

message(STATUS "Using libpsl version: ${LIBPSL_VERSION}")

# Compute numerical value
math(EXPR LIBPSL_VERSION_VALUE "(${LIBPSL_VERSION_MAJOR} << 16) + (${LIBPSL_VERSION_MINOR} << 8) + ${LIBPSL_VERSION_PATCH}")

# Convert to padded hex string without 0x
string(HEX ${LIBPSL_VERSION_VALUE} LIBPSL_HEX_VALUE)
string(LENGTH "${LIBPSL_HEX_VALUE}" HEX_LEN)
math(EXPR PAD_LEN "6 - ${HEX_LEN}")
if(PAD_LEN GREATER 0)
    string(REPEAT "0" ${PAD_LEN} PADDING)
    set(LIBPSL_HEX_VALUE "${PADDING}${LIBPSL_HEX_VALUE}")
endif()

# Set hex with 0x for substitution
set(LIBPSL_VERSION_NUMBER "0x${LIBPSL_HEX_VALUE}")

set(PACKAGE_NAME "libpsl")
set(PACKAGE_VERSION "${LIBPSL_VERSION}")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME "libpsl")
set(PACKAGE_URL "https://github.com/rockdaboot/libpsl")
set(PACKAGE_BUGREPORT "tim.ruehsen@gmx.de")

# Generated dir
set(PSL_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/psl")
file(MAKE_DIRECTORY "${PSL_BINARY_DIR}")

# Generate libpsl.h
configure_file(
    "${LIBPSL_DIR}/include/libpsl.h.in"
    "${PSL_BINARY_DIR}/libpsl.h"
    @ONLY
)

# Generate config.h from MSVC template
configure_file(
    "${LIBPSL_DIR}/msvc/config.h.win32.in"
    "${PSL_BINARY_DIR}/config.h"
    @ONLY
)

# Generate suffixes_dafsa.h using the fresh data file
set(PSL_DATA_FILE      "${CMAKE_CURRENT_SOURCE_DIR}/lib/sys/psl-list/public_suffix_list.dat")
set(SUFFIXES_DAFSA_H   "${PSL_BINARY_DIR}/suffixes_dafsa.h")
add_custom_command(
    OUTPUT "${SUFFIXES_DAFSA_H}"
    COMMAND ${Python3_EXECUTABLE}
            "${LIBPSL_DIR}/src/psl-make-dafsa"
            --output-format=cxx+
            "${PSL_DATA_FILE}"
            "${SUFFIXES_DAFSA_H}"
    MAIN_DEPENDENCY "${LIBPSL_DIR}/src/psl-make-dafsa"
    DEPENDS
        "${PSL_DATA_FILE}"
    WORKING_DIRECTORY "${LIBPSL_DIR}"
    COMMENT "Generating suffixes_dafsa.h from latest Public Suffix List data"
    VERBATIM
)
add_custom_target(generate_psl_files DEPENDS
    "${PSL_BINARY_DIR}/libpsl.h"
    "${PSL_BINARY_DIR}/config.h"
    "${SUFFIXES_DAFSA_H}"
)

add_library(psl STATIC
    "${LIBPSL_DIR}/src/lookup_string_in_fixed_set.c"
    "${LIBPSL_DIR}/src/psl.c"
)
target_compile_definitions(psl PRIVATE
	HAVE_CONFIG_H=1
    -D_CRT_SECURE_NO_WARNINGS 
    -DENABLE_BUILTIN
	# TODO: build libpsl (and curl) with libidn2
	# WITH_LIBIDN2=1
	# ENABLE_RUNTIME=libidn2
)
target_compile_definitions(psl PUBLIC PSL_STATIC)
target_compile_options(psl PRIVATE -D_CRT_SECURE_NO_WARNINGS)
target_include_directories(psl PUBLIC "${PSL_BINARY_DIR}")
add_dependencies(psl generate_psl_files)

set(LIBPSL_LIBRARY psl CACHE INTERNAL "")
set(LIBPSL_INCLUDE_DIR "${PSL_BINARY_DIR}" CACHE INTERNAL "")
target_link_libraries(SuperBLT psl)

message(STATUS "libpsl generated files in: ${PSL_BINARY_DIR}")



#### zlib-ng ####

set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)  # Ensures zlib-ng mimics zlib 1.2.x API
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)

add_subdirectory(lib/sys/zlib EXCLUDE_FROM_ALL)

# Link to the expected CURL-compatible zlib target
set(ZLIB_LIBRARY zlib-ng CACHE INTERNAL "")
set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} CACHE INTERNAL "")
target_link_libraries(SuperBLT zlib-ng)



#### CURL ####

# Use Windows SSPI for HTTPS
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)

# Turn off all the crap we don't want
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE) # disable all protocols, but http(s) and ws(s)
set(ENABLE_UNIX_SOCKETS OFF CACHE BOOL "" FORCE) # disable unix-socket support [ http(s):// ]
set(CURL_DISABLE_NEGOTIATE_AUTH ON CACHE BOOL "" FORCE) # included in SSPI
set(CURL_DISABLE_KERBEROS_AUTH ON CACHE BOOL "" FORCE) # included in SSPI
set(CURL_DISABLE_NTLM ON CACHE BOOL "" FORCE) # included in SSPI
set(CURL_DISABLE_WEBSOCKETS ON CACHE BOOL "" FORCE) # disable websocket support [ ws(s):// ]

# link curl against our included static zlib, to enable it's libz features
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)        # prevent it from trying to find system zlib
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON CACHE BOOL "" FORCE)  # disable external find
set(ZLIB_LIBRARY zlib-ng CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} CACHE PATH "" FORCE)

add_subdirectory(lib/sys/curl EXCLUDE_FROM_ALL)

target_link_libraries(SuperBLT libcurl_static)



#### LuaJIT ####

add_subdirectory(src/luajit EXCLUDE_FROM_ALL)
target_link_libraries(SuperBLT lua51)



###############################################################################
## loader binaries (WSOCK32.dll and IPHLPAPI.dll are the same code) ###########
###############################################################################

# MSVC assembles MASM, MinGW takes GNU as in Intel syntax. See loader/README.md
if(MSVC)
	set(LOADER_ASM_SYNTAX masm)
	set(LOADER_GEN_ASM "${CMAKE_CURRENT_BINARY_DIR}/loader/loader_asm.gen.asm")
	set(LOADER_MANUAL_ASM loader/loader.asm)
	set_property(SOURCE "${LOADER_MANUAL_ASM}" "${LOADER_GEN_ASM}" PROPERTY LANGUAGE ASM_MASM)
else()
	set(LOADER_ASM_SYNTAX gas)
	set(LOADER_GEN_ASM "${CMAKE_CURRENT_BINARY_DIR}/loader/loader_asm.gen.S")
	set(LOADER_MANUAL_ASM loader/loader_gas.S)
endif()
set(LOADER_GEN_H "${CMAKE_CURRENT_BINARY_DIR}/loader/loader_asm.gen.h")
set(LOADER_GEN_DEF "${CMAKE_CURRENT_BINARY_DIR}/loader/loader_asm.gen.def")
add_custom_command(
	OUTPUT "${LOADER_GEN_ASM}" "${LOADER_GEN_H}" "${LOADER_GEN_DEF}"
	COMMAND ${Python3_EXECUTABLE} loader/generate_asm.py --syntax ${LOADER_ASM_SYNTAX} --asm "${LOADER_GEN_ASM}" --header "${LOADER_GEN_H}" --def "${LOADER_GEN_DEF}"
	MAIN_DEPENDENCY loader/generate_asm.py
	DEPENDS loader/iphp.def loader/wsock.def
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	VERBATIM
)

# Both loader targets use the generated files, so generate them once up front. Otherwise the targets would each run the
# generator, which races with parallel builds (MSBuild).
add_custom_target(generate_loader_asm DEPENDS "${LOADER_GEN_ASM}" "${LOADER_GEN_H}" "${LOADER_GEN_DEF}")

macro(Add_Loader library_name)
	add_library(${library_name} SHARED loader/loader.cpp ${LOADER_MANUAL_ASM} src/Resource.rc "${LOADER_GEN_ASM}" "${LOADER_GEN_DEF}")
	add_dependencies(${library_name} generate_loader_asm)
	target_link_libraries(${library_name} LINK_PRIVATE SuperBLT)
	target_include_directories(${library_name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/loader")
endmacro()

Add_Loader(WSOCK32)
Add_Loader(IPHLPAPI)

###############################################################################
## build external updater for dll                                            ##
###############################################################################
add_executable(SBLT_DLL_UPDATER updater/updater.cpp updater/compression.cpp updater/Resource.rc)
if(MSVC)
	target_link_libraries(SBLT_DLL_UPDATER crypt32.lib ws2_32.lib version.lib)
else()
	target_link_libraries(SBLT_DLL_UPDATER crypt32 ws2_32 version)
endif()
target_link_libraries(SBLT_DLL_UPDATER psl zlib-ng libcurl_static)
if(NOT MSVC)
	target_link_options(SBLT_DLL_UPDATER PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--gc-sections $<$<CONFIG:Release,MinSizeRel>:-s>)
endif()
target_include_directories(SBLT_DLL_UPDATER PRIVATE updater)