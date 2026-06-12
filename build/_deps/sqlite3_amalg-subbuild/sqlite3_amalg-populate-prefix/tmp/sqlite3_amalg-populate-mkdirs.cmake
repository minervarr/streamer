# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-src")
  file(MAKE_DIRECTORY "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-build"
  "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-subbuild/sqlite3_amalg-populate-prefix"
  "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-subbuild/sqlite3_amalg-populate-prefix/tmp"
  "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-subbuild/sqlite3_amalg-populate-prefix/src/sqlite3_amalg-populate-stamp"
  "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-subbuild/sqlite3_amalg-populate-prefix/src"
  "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-subbuild/sqlite3_amalg-populate-prefix/src/sqlite3_amalg-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-subbuild/sqlite3_amalg-populate-prefix/src/sqlite3_amalg-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/_deps/sqlite3_amalg-subbuild/sqlite3_amalg-populate-prefix/src/sqlite3_amalg-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
