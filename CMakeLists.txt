project(vhost-user-qti)

cmake_minimum_required(VERSION 3.10)

set (VHOST_USER_LIB_INC_PATH ${CMAKE_CURRENT_LIST_DIR}/include/)

set(SOURCE_FILES
      src/vhost_user.c
      src/socket.c
      src/vhost_kernel.c
      src/main.c
      src/vhost_user_vmm.c
      )

include(CheckSymbolExists)

check_symbol_exists(strlcat "string.h" HAVE_STRLCAT)
if(HAVE_STRLCAT)
  add_definitions(-DHAVE_STRLCAT)
endif()

check_symbol_exists(strlcpy "string.h" HAVE_STRLCPY)
if(HAVE_STRLCPY)
  add_definitions(-DHAVE_STRLCPY)
endif()

if(NOT HAVE_STRLCAT OR NOT HAVE_STRLCPY)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLIB glib-2.0)
  if (GLIB_FOUND)
    include_directories(${GLIB_INCLUDE_DIRS})
    link_directories(${GLIB_LIBRARY_DIRS})
    add_definitions(-DUSE_GLIB)
  else()
    message(FATAL_ERROR "glib-2.0 is required but not found.")
  endif()
endif()

SET(CMAKE_INSTALL_PREFIX "/usr/")

add_executable (vhost-user-qti ${SOURCE_FILES})
include_directories (${SYSROOTINC_PATH})
include_directories (${SYSROOT_INCLUDEDIR})
include_directories (${USERINCLUDE})
include_directories (${USERINCLUDE1})
include_directories (${VHOST_USER_LIB_INC_PATH})
INCLUDE_DIRECTORIES(.)

link_directories(${SYSROOT_LIBDIR})
link_directories(${CMAKE_INSTALL_LIBDIR})

target_link_libraries (vhost-user-qti systemd)

if(NOT HAVE_STRLCAT OR NOT HAVE_STRLCPY)
  target_link_libraries (vhost-user-qti vmm_client glib-2.0)
else()
  target_link_libraries (vhost-user-qti vmm_client)
endif()

install(TARGETS "vhost-user-qti" DESTINATION bin)
