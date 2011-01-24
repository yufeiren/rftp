all: rftp

include ../MCONFIG
include ../MRULES

ifeq ($(USE_READLINE),1)
CFLAGS += -D__USE_READLINE__
LIBS += -lreadline $(LIBTERMCAP) -libverbs -lrdmacm -lpthread
endif

rftp: cmds.o cmdtab.o domacro.o ftp.o glob.o main.o ruserpass.o rdma.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

domacro.o ftp.o glob.o main.o ruserpass.o: ftp_var.h pathnames.h
cmds.o cmdtab.o: ftp_var.h pathnames.h cmds.h
cmds.o glob.o: glob.h
rdma.o: rdma.h

install: rftp
	install -s -m$(BINMODE) rftp $(INSTALLROOT)$(BINDIR)

clean:
	rm -f *.o rftp

