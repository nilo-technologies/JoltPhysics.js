cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

FIND_PACKAGE(Python3)
set(PYTHON ${Python3_EXECUTABLE} CACHE STRING "Python path")
set(EMSCRIPTEN_ROOT $ENV{EMSDK}/upstream/emscripten CACHE STRING "Emscripten path")
set(CMAKE_TOOLCHAIN_FILE ${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake)
set(WEBIDL_BINDER_SCRIPT ${EMSCRIPTEN_ROOT}/tools/webidl_binder.py)
set(JOLT_FRONT_MATTER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/front-matter.js)
set(JOLT_HEADER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/JoltJS.h)
set(JOLT_IDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/JoltJS.idl)
set(OUTPUT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/dist/)
set(JOLT_TYPING ${OUTPUT_FOLDER}/types.d.ts)
# Python helpers for IDL concat / sed-like edits (Windows Ninja uses cmd.exe — no cat/sed).
set(JOLT_BUILD_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/build-tools/jolt_codegen_helpers.py")

# Option to turn on memory profiling
option(ENABLE_MEMORY_PROFILER "Enable emscriptens memory profiler to help find leaks" OFF)

# Option to turn on double precision
option(DOUBLE_PRECISION "Compile the library in double precision mode" OFF)

# Option to turn multi threading on
option(ENABLE_MULTI_THREADING "Compile the library in multi threading mode (warning: doesn't work in Safari nor with any JS callbacks currently)'" OFF)

# Option to turn on SIMD
option(ENABLE_SIMD "Enable SIMD instructions (warning: last major browser to support this was Safari 16.4 in March 2023)" OFF)

# Option to speed up compilation by only compiling the WASM compat version
option(BUILD_WASM_COMPAT_ONLY "Compile online the WASM compat version of the library (speeds up compilation)" OFF)

# Enable for resizable heap, with some amount of slowness
option(ALLOW_MEMORY_GROWTH "Allow Memory Growth" OFF)

# Default initial memory size is 128*1024*1024 = 128MB
set(INITIAL_MEMORY 134217728 CACHE STRING "Initial Memory")

# Default maximum memory size is 2*1024*1024*1024 = 2GB (only used when ALLOW_MEMORY_GROWTH is true)
set(MAXIMUM_MEMORY 2147483648 CACHE STRING "Maximum Memory")

# The configurations we support
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Distribution")

# No floating point exception checking
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)

# Turn off LTO
set(INTERPROCEDURAL_OPTIMIZATION OFF)

# Override debug symbols.
#
# Nilo fork: emit full DWARF in Debug builds (level 3) so the Chrome "C/C++ DevTools
# Support (DWARF)" extension can attach to the wasm module for line-level + locals
# C++ debugging. Previously "source-map" emitted a sidecar .wasm.map, which:
#   * Is not understood by the DWARF DevTools extension.
#   * Is unreachable in the wasm-compat single-file layout (wasm bytes embedded in JS,
#     so the wasm has no URL for a relative sourceMappingURL to resolve against).
# Trade-off: the Debug wasm-compat bundle grows substantially because DWARF sections
# are embedded inside the wasm bytes. Debug builds are intended for local development
# (Nilo opts in via NILO_JOLT_DEBUG=true), not production.
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
	set(GENERATE_DEBUG_SYMBOLS ON)
	set(JPH_DEBUG_SYMBOL_FORMAT "3" CACHE STRING "")
else()
	set(GENERATE_DEBUG_SYMBOLS OFF)
endif()

# Turn off custom allocator
set(DISABLE_CUSTOM_ALLOCATOR ON)

# Turn off ObjectStream
set(ENABLE_OBJECT_STREAM OFF)

# Use 32-bit object layers to support more bits in ObjectLayerPairFilterMask
set(OBJECT_LAYER_BITS 32)

# Enable multi threading
if (ENABLE_MULTI_THREADING)
	set(MULTI_THREADED_FLAG
		-pthread
		--post-js ${CMAKE_CURRENT_SOURCE_DIR}/multi-threaded.js
		-s SHARED_MEMORY)
	set(ENVIRONMENT_FLAG
		-s ENVIRONMENT=web,node,worker
		-s PTHREAD_POOL_SIZE=16)
	set(CMAKE_CXX_FLAGS "-pthread -s SHARED_MEMORY")
	set(OUTPUT_BASE_NAME "${OUTPUT_FOLDER}jolt-physics.multithread")
	# No thread_local stripping for MT; use no-op so COMMAND list is always valid on Windows Ninja.
	set(GLUE_STRIP_THREAD_LOCAL "${CMAKE_COMMAND}" -E true)
else()
	set(MULTI_THREADED_FLAG "")
	set(ENVIRONMENT_FLAG -s ENVIRONMENT=web,node,worker)
	set(OUTPUT_BASE_NAME "${OUTPUT_FOLDER}jolt-physics")
	# Tried to keep this multi-platform since windows/mac/linux all treat -i different,
	# though this creates extra backup file in a non-tracked folder.
	# Removing 'thread_local' from the generated binding code fixes a small 'residual' allocation
	# in the proper_cleanup.html demo and it is not needed when in single threaded mode.
	# See: https://github.com/jrouwe/JoltPhysics.js/discussions/222#discussioncomment-11381106
	# Use Python so Ninja on Windows (cmd.exe) does not require sed.
	set(GLUE_STRIP_THREAD_LOCAL
		"${Python3_EXECUTABLE}" "${JOLT_BUILD_TOOLS}" strip-thread-local glue.cpp)
endif()

# Optional suffix injected into dist/ artifact basenames so a single dist/ folder can hold
# both Release-named and Debug-named artifacts side-by-side after a publish build (e.g.
# jolt-physics.wasm.js + jolt-physics.debug.wasm.js). build-windows.ps1 / build.sh pass
# -DJPH_OUTPUT_NAME_SUFFIX=.debug for the Debug preamble; iter-build leaves this empty so
# Debug iter outputs keep the plain .wasm.js filename Vite's local-iter alias expects.
if (NOT DEFINED JPH_OUTPUT_NAME_SUFFIX)
	set(JPH_OUTPUT_NAME_SUFFIX "")
endif()
set(OUTPUT_BASE_NAME "${OUTPUT_BASE_NAME}${JPH_OUTPUT_NAME_SUFFIX}")

# Replace 'replace_by_import' by 'import' in the generated JS code to workaround a bug in emscripten
# See: https://github.com/jrouwe/JoltPhysics.js/issues/245
# Use Python so Windows builds do not require sed.
set(JOLT_REPLACE_IMPORT "${Python3_EXECUTABLE}" "${JOLT_BUILD_TOOLS}" replace-import-token)

# Enable SIMD flags
if (ENABLE_SIMD)
	set(ENABLE_SIMD_FLAG
		-msimd128
		-msse4.2)
	set(USE_WASM_SIMD ON)
else()
	set(ENABLE_SIMD_FLAG "")
	set(USE_WASM_SIMD OFF)
endif()

# Include Jolt: default is FetchContent from a Git remote + tag (no submodule). Nilo uses paired tags
# `nilo-v5.5.0` on both JoltPhysics (C++) and JoltPhysics.js forks. Override remote/tag for your GitHub org:
#   -DJOLT_PHYSICS_GIT_REPO=https://github.com/<org>/JoltPhysics -DJOLT_PHYSICS_GIT_TAG=nilo-v5.5.0
# Or use a local clone (skips fetch):
#   cmake -B Build/Debug/ST -DJOLT_PHYSICS_PATH=C:/dev/JoltPhysics ...
# Path must be the repository root (the folder that contains the Build/ directory).
# That clone should be checked out at the same nilo-* tag as this JoltPhysics.js repo when building.
set(JOLT_PHYSICS_PATH "" CACHE PATH "Local JoltPhysics C++ repo root; when set, skips GIT fetch.")

set(JOLT_PHYSICS_GIT_REPO "https://github.com/nilo-technologies/JoltPhysics" CACHE STRING "FetchContent: JoltPhysics C++ Git URL (set to your fork).")
set(JOLT_PHYSICS_GIT_TAG "nilo-v5.5.0" CACHE STRING "FetchContent: tag on JOLT_PHYSICS_GIT_REPO (paired with this repo's nilo-* tag). If unset upstream, use v5.5.0 or a local path.")

include(FetchContent)
if(JOLT_PHYSICS_PATH)
	get_filename_component(JOLT_PHYSICS_PATH_RESOLVED "${JOLT_PHYSICS_PATH}" ABSOLUTE)
	if(NOT EXISTS "${JOLT_PHYSICS_PATH_RESOLVED}/Build/CMakeLists.txt")
		message(FATAL_ERROR "JOLT_PHYSICS_PATH does not look like a JoltPhysics repo root (missing Build/CMakeLists.txt): ${JOLT_PHYSICS_PATH_RESOLVED}")
	endif()
	FetchContent_Declare(
		JoltPhysics
		SOURCE_DIR "${JOLT_PHYSICS_PATH_RESOLVED}"
		SOURCE_SUBDIR "Build"
	)
else()
	FetchContent_Declare(
		JoltPhysics
		GIT_REPOSITORY ${JOLT_PHYSICS_GIT_REPO}
		GIT_TAG ${JOLT_PHYSICS_GIT_TAG}
		SOURCE_SUBDIR "Build"
	)
endif()
FetchContent_MakeAvailable(JoltPhysics)

# FetchContent lowercases the dependency name for populated *_SOURCE_DIR variables:
# ``joltphysics_SOURCE_DIR`` for ``FetchContent_Declare(JoltPhysics ...)``, not
# ``JoltPhysics_SOURCE_DIR``. Using the wrong variable name breaks Linux CI: the
# C++ prefix map never matches, the JS map applies first to paths like
# ``.../JoltPhysics.js/JoltPhysics/Jolt/...``, and DWARF ends up with the broken
# nested prefix ``JoltPhysics.js/JoltPhysics/...`` (Chrome then resolves sources
# under the JoltPhysics.js repo clone instead of the sibling JoltPhysics C++ tree).
#
# When ``-DJOLT_PHYSICS_PATH=...`` is set (CI: ``NILO_CI_JOLT_PHYSICS_PATH``), always
# take the C++ repo root from that cache variable. FetchContent's
# ``joltphysics_SOURCE_DIR`` has been observed to equal the JS workspace root on
# Linux while Ninja still compiles ``Jolt/**.cpp`` from the sibling checkout; the
# C++ ``-fdebug-prefix-map`` must use the same absolute prefix as the compile paths.
if(JOLT_PHYSICS_PATH)
	get_filename_component(JPH_JOLT_PHYSICS_SOURCE_ROOT "${JOLT_PHYSICS_PATH}" ABSOLUTE)
	if(NOT EXISTS "${JPH_JOLT_PHYSICS_SOURCE_ROOT}/Jolt/Jolt.h")
		message(FATAL_ERROR "JOLT_PHYSICS_PATH must be the JoltPhysics C++ repo root (expected Jolt/Jolt.h): ${JPH_JOLT_PHYSICS_SOURCE_ROOT}")
	endif()
elseif(DEFINED joltphysics_SOURCE_DIR AND NOT "${joltphysics_SOURCE_DIR}" STREQUAL "")
	set(JPH_JOLT_PHYSICS_SOURCE_ROOT "${joltphysics_SOURCE_DIR}")
elseif(DEFINED JoltPhysics_SOURCE_DIR AND NOT "${JoltPhysics_SOURCE_DIR}" STREQUAL "")
	set(JPH_JOLT_PHYSICS_SOURCE_ROOT "${JoltPhysics_SOURCE_DIR}")
else()
	message(FATAL_ERROR "FetchContent: expected joltphysics_SOURCE_DIR after MakeAvailable(JoltPhysics)")
endif()

# Canonical C++ repo root: upstream Jolt sets ``PHYSICS_REPO_ROOT`` to
# ``${CMAKE_CURRENT_SOURCE_DIR}/../`` from ``Build/CMakeLists.txt``, which Ninja
# often emits as ``-I.../JoltPhysics/Build/..``. Glue uses ``-I`` on
# ``joltphysics_SOURCE_DIR`` (normalized clone root). That spelling mismatch can
# interact badly with ``-fdebug-prefix-map`` / DWARF. Force the ``Jolt`` target to
# prefer the same absolute root as glue and prefix maps (REALPATH collapses ``..``).
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.19")
	get_filename_component(JPH_JOLT_PHYSICS_SOURCE_ROOT "${JPH_JOLT_PHYSICS_SOURCE_ROOT}" REALPATH)
else()
	get_filename_component(JPH_JOLT_PHYSICS_SOURCE_ROOT "${JPH_JOLT_PHYSICS_SOURCE_ROOT}" ABSOLUTE)
endif()
if(TARGET Jolt)
	target_include_directories(Jolt BEFORE PUBLIC "$<BUILD_INTERFACE:${JPH_JOLT_PHYSICS_SOURCE_ROOT}>")
endif()

# Nilo fork: portable DWARF source paths for in-browser C++ debugging.
#
# Without this, DWARF records absolute build-host paths (e.g.
# /home/runner/work/JoltPhysics.js/JoltPhysics.js/glue.cpp on CI, or a developer's
# local C:/dev/... path on a fork build). Chrome's "C/C++ DevTools Support (DWARF)"
# extension then can't locate sources on any other machine without per-user path
# substitutions in the extension settings.
#
# We do two things together:
#
# 1. `-fdebug-prefix-map=<absolute>=<short>` rewrites the FILENAME portion of
#    DWARF entries so each translation unit records a stable, relative-looking
#    name instead of the host's absolute path:
#      <JoltPhysics.js repo root>  ->  JoltPhysics.js/
#      <JoltPhysics C++ repo root> ->  JoltPhysics/
#
# 2. `-fdebug-compilation-dir=.` overrides DW_AT_comp_dir (the compile-unit
#    directory) so it records as "." instead of the absolute CMake binary
#    directory (e.g. /home/runner/.../_deps/joltphysics-build/). DevTools
#    concatenates DW_AT_comp_dir + filename before path-substitution lookup,
#    so without this the absolute build dir leaks even when filenames are
#    cleanly rewritten, and downstream consumers see Frankenpaths like
#    `<runner-abs>/_deps/joltphysics-build/JoltPhysics/Jolt/Physics/Foo.cpp`.
#    With both flags, DWARF entries look like `./JoltPhysics/Jolt/...` and the
#    extension's "workspace folder" matching finds them via filename alone.
#
# A developer who keeps both clones as siblings (e.g. C:/dev/JoltPhysics.js and
# C:/dev/JoltPhysics) and adds the parent folder (C:/dev) as a workspace folder
# in the DWARF extension gets line-level stepping with zero manual path mapping.
#
# Notes:
# * `-fdebug-prefix-map` and `-fdebug-compilation-dir` rewrite paths in DWARF
#   only (do not change __FILE__).
# * Flags are applied to (a) the Jolt CMake target (compiles Jolt/**.cpp), (b)
#   EMCC_GLUE_ARGS (compiles generated glue.cpp), and (c) EMCC_ARGS (the emcc
#   link step, where some DWARF tables are stitched together).
# * Longest/most-specific SOURCE prefix must come first. `-fdebug-prefix-map` rules
#   apply left-to-right with first-match-wins semantics, so if a shorter source
#   prefix is listed first it will eat the path before the longer one can match.
#
#   In CI, JoltPhysics is checked out INSIDE the JoltPhysics.js workspace as
#   ``<ws>/JoltPhysics/``, so the JS-repo absolute path is a strict prefix of the
#   C++-repo absolute path. The C++-repo rule MUST be listed first or all Jolt
#   source paths end up nested as ``JoltPhysics.js/JoltPhysics/Jolt/...``.
#   Separately, the C++ rule only works if ``JPH_PREFIX_MAP_CPP_REPO`` is taken
#   from ``joltphysics_SOURCE_DIR`` (FetchContent's actual variable name), not
#   ``JoltPhysics_SOURCE_DIR`` (often unset on Linux).
# * We also pair `-fdebug-prefix-map` with `-fmacro-prefix-map` (same syntax) so
#   `__FILE__` expansions in JPH_ASSERT/etc. capture the relative path, not the
#   build-host absolute path. Without this, ~hundreds of runtime path strings
#   leak into the wasm even though DWARF itself is clean.
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
	get_filename_component(JPH_PREFIX_MAP_JS_REPO  "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
	get_filename_component(JPH_PREFIX_MAP_CPP_REPO "${JPH_JOLT_PHYSICS_SOURCE_ROOT}" ABSOLUTE)
	set(JPH_DEBUG_PREFIX_MAP_FLAGS
		# C++ repo first because in CI it sits INSIDE the JS repo (nested workspace)
		# and would otherwise be eaten by the JS-repo rule. Do not reorder.
		"-fdebug-prefix-map=${JPH_PREFIX_MAP_CPP_REPO}=JoltPhysics"
		"-fmacro-prefix-map=${JPH_PREFIX_MAP_CPP_REPO}=JoltPhysics"
		"-fdebug-prefix-map=${JPH_PREFIX_MAP_JS_REPO}=JoltPhysics.js"
		"-fmacro-prefix-map=${JPH_PREFIX_MAP_JS_REPO}=JoltPhysics.js"
		# Belt-and-suspenders: if any TU still records the broken nested prefix (e.g.
		# mis-cased FetchContent SOURCE_DIR on a host, or a linker merge artifact),
		# normalize it before ``-fdebug-compilation-dir``.
		"-fdebug-prefix-map=JoltPhysics.js/JoltPhysics=JoltPhysics"
		"-fmacro-prefix-map=JoltPhysics.js/JoltPhysics=JoltPhysics"
		"-fdebug-compilation-dir=.")
	if (TARGET Jolt)
		target_compile_options(Jolt PRIVATE ${JPH_DEBUG_PREFIX_MAP_FLAGS})
	endif()
	message(STATUS "JoltPhysics.js Debug DWARF prefix maps:")
	foreach(_jph_map IN LISTS JPH_DEBUG_PREFIX_MAP_FLAGS)
		message(STATUS "  ${_jph_map}")
	endforeach()
else()
	set(JPH_DEBUG_PREFIX_MAP_FLAGS "")
endif()

# Enable resizable heap, with some amount of slowness. Default is 128*1024*1024 = 128MB
if (ALLOW_MEMORY_GROWTH)
	set(MEMORY_GROWTH_FLAG
		-s ALLOW_MEMORY_GROWTH=1
		-s MAXIMUM_MEMORY=${MAXIMUM_MEMORY})
else()
	set(MEMORY_GROWTH_FLAG "")
endif()

# Enable memory profiler
if (ENABLE_MEMORY_PROFILER)
	set(MEMORY_PROFILER_FLAG "--memoryprofiler")
	set(INCLUDE_DEMANGLE_ALL_FLAG "-s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE='$demangleAll'")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${MEMORY_PROFILER_FLAG}")
else()
	set(MEMORY_PROFILER_FLAG "")
	set(INCLUDE_DEMANGLE_ALL_FLAG "")
endif()

# Nilo fork: gate Google Closure pass behind an option so dev iteration builds can skip
# it. Closure adds ~30s to the emcc link step (90 %+ of incremental rebuild time once
# Ninja is doing its job on the C++ side). C++ correctness and DWARF debugging are
# unaffected by skipping it. Default ON for two reasons:
#   1. CI / publish builds must match upstream output exactly.
#   2. Closure minifies the JS glue (long identifier names -> short) which dramatically
#      shrinks the *JS source text* that Chrome DevTools' Sources panel has to render
#      when execution pauses inside Jolt code (a breakpoint, exception, etc.). Without
#      closure, the unminified glue can be 5-10 MB of JS; with closure it's ~1-2 MB.
#      Above ~3 MB DevTools' Sources panel becomes unreliable and the renderer OOMs
#      shortly after auto-switching to Sources view.
# `JPH_DEV_FAST_LINK=ON` is appropriate ONLY for iteration sessions where you do not
# need DevTools to survive a pause inside Jolt code (e.g. fast feedback on physics
# behavior changes without setting JS or C++ breakpoints).
option(JPH_DEV_FAST_LINK "Skip --closure=1 in emcc link to accelerate dev iteration. WARNING: produces a larger JS glue that can crash Chrome DevTools' Sources panel when paused inside Jolt code. Only enable for iteration sessions where DevTools is closed." OFF)
if (JPH_DEV_FAST_LINK)
	set(JPH_CLOSURE_ARGS "")
	message(STATUS "JPH_DEV_FAST_LINK=ON: skipping --closure=1 in EMCC link (dev iteration mode)")
else()
	set(JPH_CLOSURE_ARGS
		--closure=1
		--closure-args="--dynamic_import_alias=replace_by_import"
		--closure-args="--externs"
		--closure-args="${CMAKE_CURRENT_SOURCE_DIR}/extern-import.js")
endif()

set(EMCC_ARGS
  -fno-rtti
  -fno-exceptions
  --extern-pre-js ${JOLT_FRONT_MATTER_FILE}
  --post-js glue.js
  ${JPH_CLOSURE_ARGS}
  ${MULTI_THREADED_FLAG}
  --post-js ${CMAKE_CURRENT_SOURCE_DIR}/helpers.js
  ${MEMORY_GROWTH_FLAG}
  -s ALLOW_TABLE_GROWTH=1
  -s WASM_BIGINT=0
  ${ENVIRONMENT_FLAG}
  -s EXPORT_ES6=1
  -s EXPORT_NAME="Jolt"
  -s MODULARIZE=1
  -s NO_EXIT_RUNTIME=1
  -s NO_FILESYSTEM=1
  -s STACK_SIZE=1048576
  -s INITIAL_MEMORY=${INITIAL_MEMORY}
  -s EXPORTED_RUNTIME_METHODS=HEAP8,HEAPU8,HEAP16,HEAPU16,HEAP32,HEAPU32,HEAPF32,HEAPF64,UTF8ToString
  ${MEMORY_PROFILER_FLAG}
  ${INCLUDE_DEMANGLE_ALL_FLAG}
  ${ENABLE_SIMD_FLAG})

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
	set(EMCC_ARGS
		${EMCC_ARGS}
		${JPH_DEBUG_PREFIX_MAP_FLAGS}
		-g${JPH_DEBUG_SYMBOL_FORMAT}
		-s ASSERTIONS)
else()
	set(EMCC_ARGS
		${EMCC_ARGS}
		-O3)
endif()

set(EMCC_JS_ARGS ${EMCC_ARGS}
  -s AGGRESSIVE_VARIABLE_ELIMINATION=1
  -s ELIMINATE_DUPLICATE_FUNCTIONS=1
  -s SINGLE_FILE=1
  -s LEGACY_VM_SUPPORT=1
  -s WASM=0)

set(EMCC_WASM_ARGS ${EMCC_ARGS}
  -s BINARYEN_IGNORE_IMPLICIT_TRAPS=1
  -s WASM=1)

set(EMCC_WASM_COMPAT_ARGS ${EMCC_WASM_ARGS}
  -s SINGLE_FILE=1)

set(EMCC_GLUE_ARGS
  -c
  # Jolt headers live at ``<Jolt C++ repo root>/Jolt/Jolt.h``. FetchContent's
  # ``joltphysics_SOURCE_DIR`` is that repo root; do NOT add ``/..`` (that points
  # at the JoltPhysics.js workspace root and breaks ``#include "Jolt/Jolt.h"`` in
  # ``JoltJS.h`` when the C++ checkout is a sibling under ``<ws>/JoltPhysics``).
  -I${JPH_JOLT_PHYSICS_SOURCE_ROOT}
  -Wall
  -std=c++17
  -fno-rtti
  -fno-exceptions
  ${MULTI_THREADED_FLAG}
  -DJPH_OBJECT_LAYER_BITS=32
  -DJPH_DISABLE_CUSTOM_ALLOCATOR
  -include ${JOLT_HEADER_FILE}
  ${MEMORY_PROFILER_FLAG}
  ${ENABLE_SIMD_FLAG})

# Can't find a way to automatically inherit settings set by target_compile_definitions, so hardcoding defines here
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
	set(JOLT_IDL_FILE ${JOLT_IDL_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/JoltJS-DebugRenderer.idl )
	set(EMCC_GLUE_ARGS
		${EMCC_GLUE_ARGS}
		${JPH_DEBUG_PREFIX_MAP_FLAGS}
		-DJPH_DEBUG_RENDERER -DJPH_PROFILE_ENABLED -D_DEBUG)
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
	set(JOLT_IDL_FILE ${JOLT_IDL_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/JoltJS-DebugRenderer.idl )
	set(EMCC_GLUE_ARGS
		${EMCC_GLUE_ARGS}
		-DJPH_DEBUG_RENDERER -DJPH_PROFILE_ENABLED -DNDEBUG)
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "Distribution")
	set(EMCC_GLUE_ARGS
		${EMCC_GLUE_ARGS}
		-DNDEBUG)
else()
	message("Unknown CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
endif()

# Add double precision define
if (DOUBLE_PRECISION)
	set(EMCC_GLUE_ARGS ${EMCC_GLUE_ARGS} -DJPH_DOUBLE_PRECISION)
endif()

project("JoltPhysics.js")

add_custom_command(
	OUTPUT ${JOLT_TYPING}
	COMMAND npx webidl-dts-gen -e -d -i jolt.idl -o ${JOLT_TYPING} -n Jolt
	DEPENDS jolt.idl
	COMMENT "Generating JoltPhysics.js typing"
	VERBATIM)

add_custom_command(
	OUTPUT glue.cpp glue.js jolt.idl
	BYPRODUCTS parser.out WebIDLGrammar.pkl
	COMMAND ${Python3_EXECUTABLE} "${JOLT_BUILD_TOOLS}" concat-idl jolt.idl ${JOLT_IDL_FILE}
	COMMAND ${PYTHON} ${WEBIDL_BINDER_SCRIPT} jolt.idl glue
	COMMAND ${GLUE_STRIP_THREAD_LOCAL}
	DEPENDS ${JOLT_IDL_FILE} "${JOLT_BUILD_TOOLS}"
	COMMENT "Generating JoltPhysics.js bindings"
	VERBATIM)

add_custom_command(
	OUTPUT glue.o
	COMMAND emcc glue.cpp ${EMCC_GLUE_ARGS} -o glue.o
	DEPENDS glue.cpp ${JOLT_HEADER_FILE}
	COMMENT "Building JoltPhysics.js bindings"
	VERBATIM)
add_custom_target(jolt-bindings ALL DEPENDS glue.js glue.o ${JOLT_TYPING})

if (NOT BUILD_WASM_COMPAT_ONLY)
	if (NOT ENABLE_MULTI_THREADING # Not compatible with multi threading due to error: 'void wasm::I64ToI32Lowering::visitStore(Store *): Assertion `!curr->isAtomic && "atomic store not implemented"' failed.'
		AND NOT ENABLE_SIMD) # Not compatible with SIMD due to error: 'unhandled unaligned load'
		add_custom_command(
			OUTPUT ${OUTPUT_BASE_NAME}.js
			COMMAND emcc glue.o $<TARGET_FILE:Jolt> ${EMCC_JS_ARGS} -o ${OUTPUT_BASE_NAME}.js
			DEPENDS jolt-bindings ${JOLT_FRONT_MATTER_FILE} ${JOLT_TARGETS} ${JOLT_IDL_FILE} ${JOLT_HEADER_FILE}
			COMMENT "Building JoltPhysics.js javascript"
			VERBATIM)
		add_custom_target(jolt-javascript ALL DEPENDS ${OUTPUT_BASE_NAME}.js)
	endif()

	add_custom_command(
		OUTPUT ${OUTPUT_BASE_NAME}.wasm.js ${OUTPUT_BASE_NAME}.wasm.wasm
		COMMAND emcc glue.o $<TARGET_FILE:Jolt> ${EMCC_WASM_ARGS} -o ${OUTPUT_BASE_NAME}.wasm.js
		COMMAND ${JOLT_REPLACE_IMPORT} ${OUTPUT_BASE_NAME}.wasm.js
		DEPENDS jolt-bindings ${JOLT_FRONT_MATTER_FILE} ${JOLT_TARGETS} ${JOLT_IDL_FILE} ${JOLT_HEADER_FILE}
		COMMENT "Building JoltPhysics.js webassembly"
		VERBATIM)
	add_custom_target(jolt-wasm ALL DEPENDS ${OUTPUT_BASE_NAME}.wasm.js ${OUTPUT_BASE_NAME}.wasm.wasm)
endif()

add_custom_command(
	OUTPUT ${OUTPUT_BASE_NAME}.wasm-compat.js
	COMMAND emcc glue.o $<TARGET_FILE:Jolt> ${EMCC_WASM_COMPAT_ARGS} -o ${OUTPUT_BASE_NAME}.wasm-compat.js
	COMMAND ${JOLT_REPLACE_IMPORT} ${OUTPUT_BASE_NAME}.wasm-compat.js
	DEPENDS jolt-bindings ${JOLT_FRONT_MATTER_FILE} ${JOLT_TARGETS} ${JOLT_IDL_FILE} ${JOLT_HEADER_FILE}
	COMMENT "Building JoltPhysics.js compat webassembly"
	VERBATIM)
add_custom_target(jolt-wasm-compat ALL DEPENDS ${OUTPUT_BASE_NAME}.wasm-compat.js)