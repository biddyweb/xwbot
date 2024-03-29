INCLUDE (CheckIncludeFiles)
INCLUDE (CheckFunctionExists)

CHECK_INCLUDE_FILES("mswsock.h" HAVE_MSWSOCK_H)
CHECK_INCLUDE_FILES("getopt.h" HAVE_GETOPT_H)
CHECK_INCLUDE_FILE("dlfcn.h" HAVE_DLFCN_H)
CHECK_INCLUDE_FILE("inttypes.h" HAVE_INTTYPES_H)
CHECK_INCLUDE_FILE("memory.h" HAVE_MEMORY_H)
CHECK_INCLUDE_FILE("poll.h" HAVE_POLL_H)
CHECK_INCLUDE_FILE("sys/epoll.h" HAVE_SYS_EPOLL_H)
CHECK_INCLUDE_FILE("sys/eventfd.h" HAVE_SYS_EVENTFD_H)
CHECK_INCLUDE_FILE("sys/event.h" HAVE_SYS_EVENT_H)
CHECK_INCLUDE_FILE("sys/inotify.h" HAVE_SYS_INOTIFY_H)
CHECK_INCLUDE_FILE("sys/select.h" HAVE_SYS_SELECT_H)
CHECK_INCLUDE_FILE("sys/signalfd.h" HAVE_SYS_SIGNALFD_H)
CHECK_INCLUDE_FILE("sys/stat.h" HAVE_SYS_STAT_H)
CHECK_INCLUDE_FILE("sys/types.h" HAVE_SYS_TYPES_H)
CHECK_INCLUDE_FILE("unistd.h" HAVE_UNISTD_H)

CHECK_FUNCTION_EXISTS (strdup HAVE_STRDUP)
CHECK_FUNCTION_EXISTS (strlen HAVE_STRLEN)
CHECK_FUNCTION_EXISTS (strcpy HAVE_STRCPY)
CHECK_FUNCTION_EXISTS (inet_ntop HAVE_INET_NTOP)
CHECK_FUNCTION_EXISTS (sendmsg HAVE_SENDMSG)
CHECK_FUNCTION_EXISTS (clock_gettime HAVE_CLOCK_GETTIME)
CHECK_FUNCTION_EXISTS (epoll_ctl HAVE_EPOLL_CTL)
CHECK_FUNCTION_EXISTS (eventfd HAVE_EVENTFD)
CHECK_FUNCTION_EXISTS (floor HAVE_FLOOR)
CHECK_FUNCTION_EXISTS (inotify_init HAVE_INOTIFY_INIT)
CHECK_FUNCTION_EXISTS (kqueue HAVE_KQUEUE)
CHECK_FUNCTION_EXISTS (nanosleep HAVE_NANOSLEEP)
CHECK_FUNCTION_EXISTS (poll HAVE_POLL)
CHECK_FUNCTION_EXISTS (select HAVE_SELECT)
CHECK_FUNCTION_EXISTS (listen HAVE_LISTEN)
CHECK_FUNCTION_EXISTS (port_create HAVE_PORT_CREATE)
CHECK_FUNCTION_EXISTS (getopt_long HAVE_GETOPT_LONG)
CHECK_FUNCTION_EXISTS (gettid HAVE_GETTID)
CHECK_FUNCTION_EXISTS (syscall HAVE_SYSCALL)

IF(WIN32)
CHECK_FUNCTION_EXISTS (WSARecvMsg HAVE_WSA_RECVMSG)
CHECK_INCLUDE_FILES("mswsock.h" HAVE_MSWSOCK_H)
CHECK_INCLUDE_FILES("winsock2.h" HAVE_WINSOCK2_H)
ELSE(WIN32)
CHECK_FUNCTION_EXISTS (recvmsg HAVE_RECVMSG)
CHECK_INCLUDE_FILES("pthread.h" HAVE_PTHREAD_H)
CHECK_INCLUDE_FILES("sys/uio.h" HAVE_SYS_UIO_H)
ENDIF(WIN32)


