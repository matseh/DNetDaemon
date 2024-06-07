
#	DNET SERVERS
#
#
#	DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved

LIB = ../lib/dnetlib.o ../lib/dnetutil.o
BIN = ../bin

all: lib $(BIN)/scopy $(BIN)/sshell $(BIN)/sloadav $(BIN)/sgcopy $(BIN)/snfs

lib:
	$(MAKE) -C ../lib

$(BIN)/%: %.o lib
	cc $< $(LIB) -o $@

%.o: %.c lib
	cc -c $< -o $@

clean:
	rm -f *.o

