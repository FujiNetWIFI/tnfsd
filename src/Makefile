CC=gcc

# Auto-detect OS if not provided. Map uname output to expected values.
ifndef OS
    UNAME_S := $(shell uname -s 2>/dev/null)
    ifeq ($(UNAME_S),Darwin)
        OS := BSD
    else ifeq ($(UNAME_S),Linux)
        OS := LINUX
    else ifeq ($(UNAME_S),Windows_NT)
        OS := Windows_NT
    else
        $(error Unknown OS. Please run `make OS=LINUX|BSD|Windows_NT`)
    endif
endif

ifeq ($(OS),LINUX)
    FLAGS = -Wall -DUNIX -DNEED_BSDCOMPAT
    EXOBJS = strlcpy.o strlcat.o event_epoll.o 
    LIBS =
    EXEC = tnfsd
endif
ifeq ($(OS),Windows_NT)
    FLAGS = -Wall -DWIN32 -DNEED_BSDCOMPAT
    EXOBJS = strlcpy.o strlcat.o event_select.o 
    LIBS = -lwsock32
    EXEC = tnfsd.exe
endif
ifeq ($(OS),BSD)
    FLAGS = -Wall -DUNIX -DBSD
    EXOBJS = event_kqueue.o
    LIBS =
    EXEC = tnfsd
endif

# ENABLE_CHROOT defaults to yes on Unix systems for security
# Disable with: make ENABLE_CHROOT=no
ifeq ($(OS),LINUX)
    ENABLE_CHROOT ?= yes
endif
ifeq ($(OS),BSD)
    ENABLE_CHROOT ?= yes
endif

ifeq ($(ENABLE_CHROOT),yes)
    CHROOTFLAGS = -DENABLE_CHROOT
endif

ifdef DEBUG
    EXFLAGS = -g -DDEBUG
endif

ifdef USAGELOG
    LOGFLAGS = -DUSAGELOG
endif

CFLAGS=$(FLAGS) $(EXFLAGS) $(LOGFLAGS) $(CHROOTFLAGS) -DNEED_ERRTABLE
OBJS=main.o datagram.o event_common.o log.o session.o endian.o directory.o errortable.o tnfs_file.o chroot.o fileinfo.o stats.o auth.o traverse.o match.o tnfsd.o $(EXOBJS)

all:	$(OBJS)
	$(CC) -o ../bin/$(EXEC) $(OBJS) $(LIBS)

clean:
	$(RM) -f $(OBJS) bin/$(EXEC)

