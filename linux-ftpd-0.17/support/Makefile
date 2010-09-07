include ../MCONFIG

OBJS=daemon.o setproctitle.o isexpired.o vis.o

all: libsupport.a

%.o: %.c
	$(CC) $(CFLAGS) -DHASSETPROCTITLE $< -c

libsupport.a: $(OBJS)
	ar -cruv $@ $^
	ranlib $@

install: ;
clean:
	rm -f *.o libsupport.a

setproctitle.o: setproctitle.h
daemon.o: daemon.h
