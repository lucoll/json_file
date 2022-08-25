cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(json_file)

find_package(ROOT REQUIRED)
include(${ROOT_USE_FILE})

ROOT_STANDARD_LIBRARY_PACKAGE(JsonFile
                              HEADERS TJSONFile.h TKeyJSON.h
                              SOURCES TJSONFile.cxx TKeyJSON.cxx
                              DEPENDENCIES ROOT::RIO)
                              
#ADD_EXECUTABLE(jsonfiletest testTJSONFile.C )

#SET(GTESTDIR
#    buildROOT/googletest-prefix/src/googletest/googletest/include/)
#TARGET_INCLUDE_DIRECTORIES(jsonfiletest PUBLIC ${GTESTDIR})
#TARGET_LINK_LIBRARIES(jsonfiletest ROOT::RIO)

