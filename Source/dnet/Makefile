
#	THE DNET DRIVER, UNIX END
# 
# 	DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
#
#	the BIN should be placed in your path for ease of use.  DNET uses 
#	UNIX level sockets allowing multiple users on a system to run it
#	independantly.  You should make a special directory (Example: .DNET)
#	and then setenv DNETDIR ~/.DNET/  ... all clients and servers will
#	use the enviroment variable to find/create the UNIX level sockets.

LIB = ../lib/dnetutil.o
BIN	= ../bin
OBJS	= dnet.o control.o files.o globals.o net.o subs.o internal.o \
	  dataconv.o packet.o

$(BIN)/dnet: lib $(OBJS)
	cc $(OBJS) $(LIB) -o $(BIN)/dnet

lib:
	$(MAKE) -C ../lib

clean:	
	rm -f *.o make.out

