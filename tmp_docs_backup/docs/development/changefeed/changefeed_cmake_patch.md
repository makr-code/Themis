````markdown
# CMake / Test registration snippet for Changefeed MVP

Füge folgende Zeilen in die passende `CMakeLists.txt` (z. B. `tests/CMakeLists.txt` oder Top‑level `CMakeLists.txt`) ein, um die Tests zu bauen und auszuführen:

```
# Add changefeed library
add_library(changefeed STATIC
  src/changefeed/changefeed.cpp
)
target_include_directories(changefeed PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Link dependencies (rocksdb, fmt, ...)
target_link_libraries(changefeed PRIVATE rocksdb fmt)

# Add test
add_executable(test_changefeed tests/test_changefeed.cpp)
target_link_libraries(test_changefeed PRIVATE changefeed gtest_main)
add_test(NAME changefeed::unit COMMAND test_changefeed)

```

Hinweis: Passe Abhängigkeiten (`rocksdb`, `gtest`, `fmt`) an die lokale CMake‑Konfiguration und vcpkg Setup an.

````
