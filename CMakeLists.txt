cmake_minimum_required(VERSION 3.8)

project(ORE CXX)

option(BUILD_DOC "Build documentation" ON)

include(CTest)

include("cmake/commonSettings.cmake")

add_subdirectory("QuantLib")
add_subdirectory("QuantExt")
add_subdirectory("OREData")
add_subdirectory("OREAnalytics")
# add examples testsuite
add_test(NAME examples WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Examples COMMAND nosetests run_examples_testsuite.py)