CC=gcc

ifndef OS
    $(error Must specify an OS with make OS=... where OS is LINUX, BSD or Windows_NT. Use BSD for macOS)
endif

ifeq ($(OS),LINUX)
    FLAGS = -Wall -DUNIX -DNEED_BSDCOMPAT -DENABLE_CHROOT -DNEED_ERRTABLE
    EXOBJS = strlcpy.o strlcat.o event_epoll.o 
    LIBS =
    EXEC = tnfsd
endif
ifeq ($(OS),Windows_NT)
    FLAGS = -Wall -DWIN32 -DNEED_ERRTABLE -DNEED_BSDCOMPAT
    EXOBJS = strlcpy.o strlcat.o event_select.o 
    LIBS = -lwsock32
    EXEC = tnfsd.exe
endif
ifeq ($(OS),BSD)
    FLAGS = -Wall -DUNIX -DENABLE_CHROOT -DNEED_ERRTABLE -DBSD
    EXOBJS = event_kqueue.o
    LIBS =
    EXEC = tnfsd
endif

ifdef DEBUG
    EXFLAGS = -g -DDEBUG
endif

ifdef USAGELOG
    LOGFLAGS = -DUSAGELOG
endif

CFLAGS=$(FLAGS) $(EXFLAGS) $(LOGFLAGS)
OBJS=main.o datagram.o event_common.o log.o session.o endian.o directory.o errortable.o tnfs_file.o chroot.o fileinfo.o stats.o auth.o $(EXOBJS)

all:	$(OBJS)
	$(CC) -o ../bin/$(EXEC) $(OBJS) $(LIBS)

clean:
	$(RM) -f $(OBJS) bin/$(EXEC)

