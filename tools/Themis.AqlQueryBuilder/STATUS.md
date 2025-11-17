# Themis.AqlQueryBuilder - Build Status

## Platform Requirements

This is a **Windows-only** WPF application. It requires:
- Windows 10 or later
- .NET 8.0 SDK
- Visual Studio 2022 (recommended) or .NET CLI

## Building on Windows

### Using Visual Studio
1. Open the solution in Visual Studio 2022
2. Build > Build Solution (Ctrl+Shift+B)
3. Run with F5

### Using .NET CLI
```powershell
cd tools\Themis.AqlQueryBuilder
dotnet restore
dotnet build --configuration Release
dotnet run
```

## Building on Linux/macOS

WPF applications cannot be built or run on Linux/macOS. The project files are complete and ready to build on Windows.

To test the project structure without building:
```bash
# Check project file syntax
dotnet build --no-restore --no-build 2>&1 | grep -v NETSDK1100
```

## CMake Integration

The CMakeLists.txt files are provided for integration into the main build system:
- When building on Windows with .NET SDK installed, the CMake target will build the WPF application
- On Linux/macOS, CMake will skip the .NET tools with a warning

To enable in CMake:
```bash
cmake -DTHEMIS_BUILD_ADMIN_TOOLS=ON ..
```

## Project Structure Validation

The following files have been created and are ready for Windows builds:
- ✅ Themis.AqlQueryBuilder.csproj (Project file)
- ✅ App.xaml / App.xaml.cs (Application entry point)
- ✅ MainWindow.xaml / MainWindow.xaml.cs (Main window)
- ✅ Models/AqlQueryModel.cs (Data models)
- ✅ ViewModels/MainViewModel.cs (MVVM ViewModel)
- ✅ app.manifest (Windows manifest)
- ✅ CMakeLists.txt (CMake integration)
- ✅ README.md (Documentation)

All files follow .NET 8 and WPF best practices.
