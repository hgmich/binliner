cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(workflow_binliner)

if(NOT BN_API_PATH)
	set(BN_API_PATH $ENV{BN_API_PATH})
	if(NOT BN_API_PATH)
		message(FATAL_ERROR "Provide path to Binary Ninja API source in BN_API_PATH")
	endif()
endif()
add_subdirectory(${BN_API_PATH} ${PROJECT_BINARY_DIR}/api)

file(GLOB SOURCES
	*.cpp
	*.h)

add_library(workflow_binliner SHARED ${SOURCES})

target_include_directories(workflow_binliner
	PRIVATE ${PROJECT_SOURCE_DIR})

target_link_libraries(workflow_binliner binaryninjaapi)

set_target_properties(workflow_binliner PROPERTIES
	CXX_STANDARD 17
	CXX_VISIBILITY_PRESET hidden
	CXX_STANDARD_REQUIRED ON
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
	POSITION_INDEPENDENT_CODE ON)

