CC=gcc
CFLAGS=-g
OBJ=tel_serv.o
LIBS=-lpthread

%.o:%.c
	$(CC) -c -o $@ $< $(CFLAGS)

server:$(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY:clean

clean:
	rm -f *.o
