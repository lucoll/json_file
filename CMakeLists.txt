cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(json_file)

find_package(ROOT REQUIRED)
include(${ROOT_USE_FILE})

ROOT_GENERATE_DICTIONARY(G__json_file  TJSONFile.h TKeyJSON.h
                         MODULE json_file
                         LINKDEF LinkDef.h
                         DEPENDENCIES ROOT:RIO)


ROOT_STANDARD_LIBRARY_PACKAGE(JsonFile
                              HEADERS TJSONFile.h TKeyJSON.h
                              SOURCES TJSONFile.cxx TKeyJSON.cxx)
