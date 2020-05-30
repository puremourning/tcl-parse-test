DEBUGFLAGS=-g -O0 -fno-omit-frame-pointer
CPPFLAGS=-I../tcl/generic -I../tcl/unix -std=c++17 ${DEBUGFLAGS}
LDFLAGS=-L../tcl/unix -ltcl9.0 -lz  -lpthread -framework CoreFoundation

parse: parse.cpp

.PHONY: clean
clean:
	rm -f parse
