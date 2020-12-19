cmake_minimum_required(VERSION 3.5)
project(ACTools NONE)

if(UNIX)
	add_subdirectory(TransperentProxy)
endif()
