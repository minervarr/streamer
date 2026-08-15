# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.22.1-g37088a8)

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(sqlite3_amalg-populate NONE)



include(ExternalProject)
ExternalProject_Add(sqlite3_amalg-populate
                     "UPDATE_DISCONNECTED" "False" "URL" "https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"
                    SOURCE_DIR          "/home/nava/Files/code/active/streamer/android/app/.cxx/Debug/1i2yu1k5/x86_64/_deps/sqlite3_amalg-src"
                    BINARY_DIR          "/home/nava/Files/code/active/streamer/android/app/.cxx/Debug/1i2yu1k5/x86_64/_deps/sqlite3_amalg-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
)


