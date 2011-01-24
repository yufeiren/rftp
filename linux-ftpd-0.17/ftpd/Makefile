include ../MCONFIG

CFLAGS+=-I../support -I..
LIBS+=-L../support -lsupport  -libverbs -lrdmacm -lpthread


OBJS=ftpd.o ftpcmd.o logutmp.o logwtmp.o popen.o rdma.o

## our internal version of ls.
# LSDIR	= ${.CURDIR}/../../bin/ls
# .PATH:	${LSDIR}
# SRCS	+= ls.c cmp.c print.c stat_flags.c util.c
# CFLAGS	+= -I${LSDIR}

## (if tcp_wrappers)
# CFLAGS+=-DTCPWRAPPERS
# LDADD+=	-lwrap

all: rftpd

%.o: %.c
	$(CC) $(CFLAGS) -DHASSETPROCTITLE $< -c

ftpcmd.c: %.c: %.y
	$(YACC) $<
	-mv -f y.tab.c $@

rftpd: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

install: rftpd
	install -s -m$(DAEMONMODE) rftpd $(INSTALLROOT)$(BINDIR)/rftpd

clean:
	rm -f *.o ftpcmd.c y.tab.h rftpd


ftpd.o: pathnames.h ../version.h
$(OBJS): extern.h ../support/daemon.h ../support/setproctitle.h
