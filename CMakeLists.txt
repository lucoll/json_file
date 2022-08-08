cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(json_file)

find_package(ROOT REQUIRED)
include(${ROOT_USE_FILE})

ROOT_STANDARD_LIBRARY_PACKAGE(JsonFile
                              HEADERS TJSONFile.h TKeyJSON.h
                              SOURCES TJSONFile.cxx TKeyJSON.cxx
                              DEPENDENCIES ROOT::RIO)
