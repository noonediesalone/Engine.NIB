include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

include(${CMAKE_CURRENT_LIST_DIR}/writeAll.cmake)

# define build type clang address sanitizer + undefined behaviour + LIBCPP assertions, but keep O2
set(CMAKE_CXX_FLAGS_CLANG_ASAN_O2 "-fsanitize=address,undefined -fno-omit-frame-pointer -D_LIBCPP_ENABLE_ASSERTIONS=1 -g -O2")

# add compiler flag, if not already present
macro(add_compiler_flag flag supportsFlag)
    check_cxx_compiler_flag(${flag} ${supportsFlag})
    if(${supportsFlag} AND NOT "${CMAKE_CXX_FLAGS}" MATCHES "${flag}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}")
    endif()
endmacro()

# add linker flag for shared libs and exe, if not already present
macro(add_linker_flag flag supportsFlag)
    check_linker_flag(CXX ${flag} ${supportsFlag})
    if(${supportsFlag} AND NOT "${CMAKE_SHARED_LINKER_FLAGS}" MATCHES "${flag}")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${flag}")
    endif()
    if(${supportsFlag} AND NOT "${CMAKE_EXE_LINKER_FLAGS}" MATCHES "${flag}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${flag}")
    endif()
endmacro()

# use CXX 17, disable gnu extensions
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS FALSE)

# If available, use PIC for shared libs and PIE for executables
if (NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
if (CMAKE_POSITION_INDEPENDENT_CODE)
    # cmake policy CMP0083: add PIE support if possible (need cmake 3.14)
    include(CheckPIESupported)
    check_pie_supported()
endif()

# set compiler macro if open cl is enabled
if (ORE_ENABLE_OPENCL)
  add_compile_definitions(ORE_ENABLE_OPENCL)
endif()

# set compiler macro if CUDA is enabled
if (ORE_ENABLE_CUDA)
  add_compile_definitions(ORE_ENABLE_CUDA)
endif()

# set compiler macro if ORE_PYTHON_INTEGRATION is set
if (ORE_PYTHON_INTEGRATION)
  add_compile_definitions(ORE_PYTHON_INTEGRATION)
endif()

# set compiler macro if zlib is enabled
if(ORE_USE_ZLIB)
  add_compile_definitions(ORE_USE_ZLIB)
endif()

# set compiler macro if cpu affinity
if(ORE_MULTITHREADING_CPU_AFFINITY)
  add_compile_definitions(ORE_MULTITHREADING_CPU_AFFINITY)
endif()


# On single-configuration builds, select a default build type that gives the same compilation flags as a default autotools build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()

if(NOT DONT_SET_QL_INCLUDE_DIR_FIRST)
   # add build/QuantLib as first include directory to make sure we include QL's cmake-configured files
    include_directories("${PROJECT_BINARY_DIR}/QuantLib")
endif()



if(MSVC)
    set(BUILD_SHARED_LIBS OFF)
    # build static libs always
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<BOOL:${MSVC_LINK_DYNAMIC_RUNTIME}>:DLL>")

    add_compile_options(/external:env:BOOST)
    add_compile_options(/external:W0)
    add_compile_definitions(_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
    add_compile_definitions(_SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING)
    add_compile_definitions(_SCL_SECURE_NO_DEPRECATE)
    add_compile_definitions(_CRT_SECURE_NO_DEPRECATE)
    add_compile_definitions(BOOST_ENABLE_ASSERT_HANDLER)
    add_compile_options(/bigobj)
    add_compile_options(/W3)
    #add_compile_options(/we4265) #no-virtual-destructor
    #add_compile_options(/we4388) # 'equality-operator' : signed/unsigned mismatch
    add_compile_options(/we5038) # reorder
    # add_compile_options(/we4101) # unreferenced local variable (too strict)
    add_compile_options(/we4189) # 'identifier' : local variable is initialized but not referenced
    add_compile_options(/we4700) # uninitialized local variable 'name' used
    add_compile_options(/we5233) # unused lambda
    add_compile_options(/we4508) # 'function' : function should return a value; 'void' return type assumed
    add_compile_options(/wd4834)
    add_compile_options(/we26815) # dangling references/pointer
    # add_compiler_flag("/we4389" signed_compare_mscv)

    add_link_options(/LARGEADDRESSAWARE)

    add_compile_options("$<$<CONFIG:Release>:/GF>")
    add_compile_options("$<$<CONFIG:Release>:/Gy>")
    add_compile_options("$<$<CONFIG:Release>:/Ot>")
    add_compile_options("$<$<CONFIG:Release>:/GT>")

    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/GF>")
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/Gy>")
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/GT>")
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/Oi>")
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/Ot>")

    if(MSVC_PARALLELBUILD)
        add_compile_options(/MP)
    endif()

else()
    if (NOT DEFINED BUILD_SHARED_LIBS)
        # build shared libs always
        set(BUILD_SHARED_LIBS ON)
    endif()

    # Issue with Boost CMake finder introduced in version 1.70
    set(Boost_NO_BOOST_CMAKE         ON)

    # avoid a crash in valgrind that sometimes occurs if this flag is not defined
    add_definitions(-DBOOST_MATH_NO_LONG_DOUBLE_MATH_FUNCTIONS)

    # switch off blas debug for build types release
    if(NOT("${CMAKE_BUILD_TYPE}" STREQUAL "Debug"))
        add_definitions(-DBOOST_UBLAS_NDEBUG)
    endif()

    # add pthread flag
    add_compiler_flag("-pthread" usePThreadCompilerFlag)
    add_linker_flag("-pthread" usePThreadLinkerFlag)

    # use flat namespace to fix symbol lookup issues and align with linux more closely
    if (APPLE)
        add_linker_flag("-flat_namespace" supportsFlatNameSpace)
    endif()

    if(QL_USE_PCH)
      # see https://ccache.dev/manual/4.8.3.html#_precompiled_headers
      add_compiler_flag("-Xclang -fno-pch-timestamp" supportsNoPchTimestamp)
      # needed for gcc, although the ccache documentation does not strictly require this
      add_compiler_flag("-fpch-preprocess" supportsPchPreprocess)
    endif()

    # enable boost assert handler
    add_compiler_flag("-DBOOST_ENABLE_ASSERT_HANDLER" enableAssertionHandler)

    # add some warnings
    add_compiler_flag("-Wall" supportsWall)
    add_compiler_flag("-Wsometimes-uninitialized" supportsSometimesUninitialized)
    add_compiler_flag("-Wmaybe-uninitialized" supportsMaybeUninitialized)

    # turn the following warnings into errors
    add_compiler_flag("-Werror=non-virtual-dtor" supportsNonVirtualDtor)
    # the line below breaks the linux build
    #add_compiler_flag("-Werror=sign-compare" supportsSignCompare)
    add_compiler_flag("-Werror=float-conversion" supportsWfloatConversion)
    add_compiler_flag("-Werror=reorder" supportsReorder)
    #add_compiler_flag("-Werror=unused-variable" supportsUnusedVariable)
    add_compiler_flag("-Werror=unused-but-set-variable" supportsUnusedButSetVariable)
    #add_compiler_flag("-Werror=uninitialized" supportsUninitialized)
    add_compiler_flag("-Werror=unused-lambda-capture" supportsUnusedLambdaCapture)
    add_compiler_flag("-Werror=return-type" supportsReturnType)
    add_compiler_flag("-Werror=unused-function" supportsUnusedFunction)
    # the line below breaks the linux build
    #add_compiler_flag("-Werror=suggest-override" supportsSuggestOverride)
    add_compiler_flag("-Werror=inconsistent-missing-override" supportsInconsistentMissingOverride)

    # disable some warnings
    add_compiler_flag("-Wno-unknown-pragmas" supportsNoUnknownPragmas)

    # disable warnings from boost
    add_compiler_flag("--system-header-prefix=boost/" supportsSystemHeaderPrefixBoost)

    add_compile_options("-Werror=unused-variable")
    add_compile_options("-Werror=uninitialized")

    # add build/QuantLib as first include directory to make sure we include QL's cmake-configured files
    # if QuantLib is build separately
    include_directories("${CMAKE_CURRENT_LIST_DIR}/../QuantLib/build")

endif()

# Boost #
# link against static boost libraries
if(NOT DEFINED Boost_USE_STATIC_LIBS)
    if(BUILD_SHARED_LIBS)
        set(Boost_USE_STATIC_LIBS OFF)
    else()
        set(Boost_USE_STATIC_LIBS ON)
    endif()
endif()

# Boost static runtime. ON for MSVC
if(NOT DEFINED Boost_USE_STATIC_RUNTIME)
    if(BUILD_SHARED_LIBS OR(MSVC AND MSVC_LINK_DYNAMIC_RUNTIME))
        set(Boost_USE_STATIC_RUNTIME OFF)
    else()
        set(Boost_USE_STATIC_RUNTIME ON)
    endif()
endif()

if(NOT Boost_USE_STATIC_LIBS)
    # link against dynamic boost libraries
    add_definitions(-DBOOST_ALL_DYN_LINK)
    add_definitions(-DBOOST_TEST_DYN_LINK)
endif()

if(ORE_BOOST_AUTO_LINK_SYSTEM)
    add_definitions(-DBOOST_AUTO_LINK_SYSTEM)
endif()

set(Boost_NO_WARN_NEW_VERSIONS ON)

if (MSVC)
    find_package(Boost)
    if(Boost_VERSION_STRING LESS 1.84.0)
        add_compile_definitions(_WINVER=0x0601)
        add_compile_definitions(_WIN32_WINNT=0x0601)
        add_compile_definitions(BOOST_USE_WINAPI_VERSION=0x0601)
    endif()
endif()
# Boost end #

# workaround when building with boost 1.81, see https://github.com/boostorg/phoenix/issues/111
add_definitions(-DBOOST_PHOENIX_STL_TUPLE_H_)

# set library locations
get_filename_component(QUANTLIB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../QuantLib" ABSOLUTE)
get_filename_component(QUANTEXT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../QuantExt" ABSOLUTE)
get_filename_component(OREDATA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../OREData" ABSOLUTE)
get_filename_component(OREANALYTICS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../OREAnalytics" ABSOLUTE)
get_filename_component(ORETEST_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../ORETest" ABSOLUTE)
get_filename_component(RAPIDXML_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../ThirdPartyLibs/rapidxml-1.13" ABSOLUTE)

# parallel unit test runner
option(ORE_ENABLE_PARALLEL_UNIT_TEST_RUNNER "Enable the parallel unit test runner" OFF)
if (ORE_ENABLE_PARALLEL_UNIT_TEST_RUNNER)
    add_definitions(-DORE_ENABLE_PARALLEL_UNIT_TEST_RUNNER)
endif()

# convenience function that adds a link directory dir, but only if it exists
function(add_link_directory_if_exists dir)
  if(EXISTS "${dir}")
    link_directories("${dir}")
  endif()
endfunction()

macro(get_library_name LIB_NAME OUTPUT_NAME)

    # modified version of quantlib.cmake / get_quantlib_library_name

    # message(STATUS "${LIB_NAME} Library name tokens:")
    # MSVC: Give built library different names following code in 'ql/autolink.hpp'
    if(MSVC)
        if (${CMAKE_SIZEOF_VOID_P} EQUAL 8)
            set(LIB_PLATFORM "-x64")
            set(LIB_PLATFORM "-x64")
        endif()
        # - thread linkage
        set(LIB_THREAD_OPT "-mt")  # _MT is defined for /MD and /MT runtimes (https://docs.microsoft.com/es-es/cpp/build/reference/md-mt-ld-use-run-time-library)
        #message(STATUS " - Thread opt: ${LIB_THREAD_OPT}")

        if (NOT BUILD_SHARED_LIBS AND (MSVC AND NOT MSVC_LINK_DYNAMIC_RUNTIME))
            set(LIB_RT_OPT "-s")
            set(CMAKE_DEBUG_POSTFIX "gd")
        else()
            set(CMAKE_DEBUG_POSTFIX "-gd")
        endif()


        set(${OUTPUT_NAME} "${LIB_NAME}${LIB_PLATFORM}${LIB_THREAD_OPT}${LIB_RT_OPT}")
    else()
        set(${OUTPUT_NAME} ${LIB_NAME})
    endif()

    message(STATUS "${LIB_NAME} library name: ${${OUTPUT_NAME}}[${CMAKE_DEBUG_POSTFIX}]")
endmacro(get_library_name)

macro(set_ql_library_name)
  if(USE_GLOBAL_ORE_BUILD)
    set(QL_LIB_NAME ql_library)
  else()
    get_library_name("QuantLib" QL_LIB_NAME)
  endif()
endmacro()

function(generate_git_hash custom_target_name)
  file(WRITE ${QUANTEXT_SOURCE_DIR}/qle/gitversion.hpp)
  add_custom_target(
    ${custom_target_name} ALL
    COMMAND ${CMAKE_COMMAND} -D IN_FILE=${QUANTEXT_SOURCE_DIR}/qle/gitversion.hpp.in -D OUT_FILE=${QUANTEXT_SOURCE_DIR}/qle/gitversion.hpp
                             -P ${QUANTEXT_SOURCE_DIR}/../cmake/generateGitVersion.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()
