# CMake configuration for Themis.AqlQueryBuilder (.NET 8 WPF Application)
# 
# This CMakeLists.txt provides integration of the .NET project into the main
# CMake build system. The actual building is done by the .NET SDK (dotnet build).

cmake_minimum_required(VERSION 3.20)

# Note: This is a .NET 8.0 WPF project and requires the .NET SDK to build
# CMake is used here only for integration into the main build system

# Check if .NET SDK is available
find_program(DOTNET_EXECUTABLE dotnet)

if(DOTNET_EXECUTABLE)
    message(STATUS "Found .NET SDK: ${DOTNET_EXECUTABLE}")
    
    # Add custom target for building the AQL Query Builder
    add_custom_target(Themis.AqlQueryBuilder ALL
        COMMAND ${DOTNET_EXECUTABLE} build "${CMAKE_CURRENT_SOURCE_DIR}/Themis.AqlQueryBuilder.csproj" 
                --configuration $<IF:$<CONFIG:Debug>,Debug,Release>
                --verbosity minimal
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building Themis.AqlQueryBuilder (.NET WPF Application)"
        VERBATIM
    )
    
    # Add custom target for running the application (optional)
    add_custom_target(run-aql-builder
        COMMAND ${DOTNET_EXECUTABLE} run --project "${CMAKE_CURRENT_SOURCE_DIR}/Themis.AqlQueryBuilder.csproj"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running Themis.AqlQueryBuilder"
        VERBATIM
        USES_TERMINAL
    )
    
    # Add custom target for publishing the application
    add_custom_target(publish-aql-builder
        COMMAND ${DOTNET_EXECUTABLE} publish "${CMAKE_CURRENT_SOURCE_DIR}/Themis.AqlQueryBuilder.csproj"
                --configuration Release
                --output "${CMAKE_BINARY_DIR}/tools/AqlQueryBuilder"
                --self-contained false
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Publishing Themis.AqlQueryBuilder"
        VERBATIM
    )
    
    # Installation target (optional)
    install(
        DIRECTORY ${CMAKE_BINARY_DIR}/tools/AqlQueryBuilder/
        DESTINATION tools/AqlQueryBuilder
        OPTIONAL
    )
    
else()
    message(WARNING "Themis.AqlQueryBuilder requires .NET SDK 8.0 or later. Skipping .NET tools build.")
    message(WARNING "Install .NET SDK from: https://dotnet.microsoft.com/download")
endif()

# Add documentation
install(
    FILES README.md
    DESTINATION share/doc/themis/tools/AqlQueryBuilder
    OPTIONAL
)
