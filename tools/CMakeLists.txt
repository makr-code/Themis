# CMake configuration for Themis Admin Tools (.NET 8 WPF Applications)
# 
# This CMakeLists.txt provides integration of .NET admin tools into the main
# CMake build system. The actual building is done by the .NET SDK (dotnet build).

cmake_minimum_required(VERSION 3.20)
project(ThemisAdminTools LANGUAGES NONE)

# Check if .NET SDK is available
find_program(DOTNET_EXECUTABLE dotnet)

if(DOTNET_EXECUTABLE)
    message(STATUS "Found .NET SDK for Themis Admin Tools: ${DOTNET_EXECUTABLE}")
    
    # Include the AQL Query Builder
    add_subdirectory(Themis.AqlQueryBuilder)
    
    # Optionally, add other .NET admin tools here in the future
    # add_subdirectory(Themis.AuditLogViewer)
    # add_subdirectory(Themis.PIIManager)
    # add_subdirectory(Themis.KeyRotationDashboard)
    # etc.
    
    message(STATUS "Themis Admin Tools (.NET) configured successfully")
else()
    message(WARNING ".NET admin tools require .NET SDK 8.0 or later")
    message(WARNING "Install .NET SDK from: https://dotnet.microsoft.com/download")
    message(WARNING "Skipping .NET admin tools build")
endif()
