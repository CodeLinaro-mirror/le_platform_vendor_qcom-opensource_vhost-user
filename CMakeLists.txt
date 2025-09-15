project(vhost-user-qti)

cmake_minimum_required(VERSION 3.10)

set (VHOST_USER_LIB_INC_PATH ${CMAKE_CURRENT_LIST_DIR}/include/)

set(SOURCE_FILES
      src/vhost_user.c
      src/socket.c
      src/vhost_kernel.c
      src/main.c
      )

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

install(TARGETS "vhost-user-qti" DESTINATION bin)
