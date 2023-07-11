project(vhost-user-qti)

cmake_minimum_required(VERSION 3.10)

set(SOURCE_FILES
  ./vhost_user_q.c
  ./vhost_ioctl.c
)

SET(CMAKE_INSTALL_PREFIX "/usr/")

add_executable (vhost-user-qti ${SOURCE_FILES})
include_directories (${SYSROOTINC_PATH})
include_directories (${SYSROOT_INCLUDEDIR})
include_directories (${USERINCLUDE})
include_directories (${USERINCLUDE1})
INCLUDE_DIRECTORIES(.)

link_directories(${SYSROOT_LIBDIR})

install(TARGETS "vhost-user-qti" DESTINATION bin)
