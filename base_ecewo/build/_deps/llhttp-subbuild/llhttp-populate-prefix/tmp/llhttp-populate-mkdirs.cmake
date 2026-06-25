# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-src")
  file(MAKE_DIRECTORY "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-src")
endif()
file(MAKE_DIRECTORY
  "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-build"
  "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-subbuild/llhttp-populate-prefix"
  "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-subbuild/llhttp-populate-prefix/tmp"
  "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-subbuild/llhttp-populate-prefix/src/llhttp-populate-stamp"
  "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-subbuild/llhttp-populate-prefix/src"
  "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-subbuild/llhttp-populate-prefix/src/llhttp-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-subbuild/llhttp-populate-prefix/src/llhttp-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/tim/cprogs/cbase/base_ecewo/build/_deps/llhttp-subbuild/llhttp-populate-prefix/src/llhttp-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
