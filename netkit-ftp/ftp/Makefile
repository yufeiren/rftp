all: rcftp

include ../MCONFIG
include ../MRULES

ifeq ($(USE_READLINE),1)
CFLAGS += -D__USE_READLINE__
LIBS += -lreadline $(LIBTERMCAP)
endif
CFLAGS += -DRCFTPRC
LIBS += -libverbs -lrdmacm -lpthread -lrt

rcftp: cmds.o cmdtab.o domacro.o ftp.o glob.o main.o ruserpass.o rdma.o utils.o init.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

domacro.o ftp.o glob.o main.o ruserpass.o: ftp_var.h pathnames.h
cmds.o cmdtab.o: ftp_var.h pathnames.h cmds.h
cmds.o glob.o: glob.h
rdma.o: rdma.h
utils.o: utils.h
init.o: init.h

install: rcftp
	install -s -m$(BINMODE) rcftp $(INSTALLROOT)$(BINDIR)

clean:
	rm -f rcftp *.o

