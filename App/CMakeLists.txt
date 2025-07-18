cmake_minimum_required(VERSION 3.15)

project(App CXX)

get_library_name("OREPlusBase" OREPBASE_LIB_NAME)
get_library_name("OREAnalytics" OREA_LIB_NAME)
get_library_name("OREData" ORED_LIB_NAME)
get_library_name("QuantExt" QLE_LIB_NAME)
set_ql_library_name()

find_package (Boost REQUIRED COMPONENTS date_time serialization filesystem timer OPTIONAL_COMPONENTS chrono)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${QUANTLIB_SOURCE_DIR})
include_directories(${QUANTEXT_SOURCE_DIR})
include_directories(${OREDATA_SOURCE_DIR})
include_directories(${OREANALYTICS_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_link_directory_if_exists("${QUANTLIB_SOURCE_DIR}/build/ql")
add_link_directory_if_exists("${QUANTEXT_SOURCE_DIR}/build/qle")
add_link_directory_if_exists("${OREDATA_SOURCE_DIR}/build/ored")
add_link_directory_if_exists("${OREANALYTICS_SOURCE_DIR}/build/orea")

add_link_directory_if_exists("${CMAKE_BINARY_DIR}/QuantLib/ql")

add_executable(ore ore.cpp)
target_link_libraries(ore ${OREA_LIB_NAME})
target_link_libraries(ore ${ORED_LIB_NAME})
target_link_libraries(ore ${QLE_LIB_NAME})
target_link_libraries(ore ${QL_LIB_NAME})
target_link_libraries(ore ${Boost_LIBRARIES})
if (ORE_PYTHON_INTEGRATION)
    target_link_libraries(ore ${Python_LIBRARIES})
endif()

if(MSVC AND MSVC_WHOLE_PROGRAM_OPTIMIZATION)
    target_compile_options(ore PRIVATE "$<$<CONFIG:RelWithDebInfo>:/GL>")
    target_link_options(ore PRIVATE "$<$<CONFIG:RelWithDebInfo>:/INCREMENTAL:NO>")
    target_link_options(ore PRIVATE "$<$<CONFIG:RelWithDebInfo>:/LTCG>")
    target_link_options(ore PRIVATE "$<$<CONFIG:RelWithDebInfo>:/OPT:REF>")
    target_link_options(ore PRIVATE "$<$<CONFIG:RelWithDebInfo>:/OPT:ICF>")

    target_compile_options(ore PRIVATE "$<$<CONFIG:Release>:/GL>")
    target_link_options(ore PRIVATE "$<$<CONFIG:Release>:/INCREMENTAL:NO>")
    target_link_options(ore PRIVATE "$<$<CONFIG:Release>:/LTCG>")
    target_link_options(ore PRIVATE "$<$<CONFIG:Release>:/OPT:REF>")
    target_link_options(ore PRIVATE "$<$<CONFIG:Release>:/OPT:ICF>")

endif()

install(TARGETS ore
        RUNTIME DESTINATION bin
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )
