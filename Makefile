TCL ?= tcl
DEBUGFLAGS=-g -O0 -fno-omit-frame-pointer
CPPFLAGS=-I$(TCL)/generic -I$(TCL)/unix -std=c++17 ${DEBUGFLAGS}
LDFLAGS=-L$(TCL)/unix -ltcl9.0 -lz  -lpthread -framework CoreFoundation

parse: parse.cpp

.PHONY: clean
clean:
	rm -f parse
