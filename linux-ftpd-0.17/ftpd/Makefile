include ../MCONFIG

CFLAGS+=-I../support
LIBS+=-L../support -lsupport


OBJS=ftpd.o ftpcmd.o logutmp.o logwtmp.o popen.o

## our internal version of ls.
# LSDIR	= ${.CURDIR}/../../bin/ls
# .PATH:	${LSDIR}
# SRCS	+= ls.c cmp.c print.c stat_flags.c util.c
# CFLAGS	+= -I${LSDIR}

## (if tcp_wrappers)
# CFLAGS+=-DTCPWRAPPERS
# LDADD+=	-lwrap

all: ftpd

%.o: %.c
	$(CC) $(CFLAGS) -DHASSETPROCTITLE $< -c

ftpcmd.c: %.c: %.y
	$(YACC) $<
	-mv -f y.tab.c $@

ftpd: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

install: ftpd
	install -s -m$(DAEMONMODE) ftpd $(INSTALLROOT)$(SBINDIR)/in.ftpd
	install -m$(MANMODE) ftpusers.5 $(INSTALLROOT)$(MANDIR)/man5/ftpusers.5
	install -m$(MANMODE) ftpd.8 $(INSTALLROOT)$(MANDIR)/man8/in.ftpd.8
	ln -sf in.ftpd.8 $(INSTALLROOT)$(MANDIR)/man8/ftpd.8

clean:
	rm -f *.o ftpcmd.c y.tab.h ftpd


ftpd.o: pathnames.h ../version.h
$(OBJS): extern.h ../support/daemon.h ../support/setproctitle.h
