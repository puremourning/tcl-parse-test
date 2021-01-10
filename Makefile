TCL ?= tcl
DEBUGFLAGS=-g -O0 -fno-omit-frame-pointer
ASANFLAGS=-fsanitize=address,undefined $(DEBUGFLAGS)
BASICFLAGS=-I$(TCL)/generic -I$(TCL)/unix -std=c++17

# put analyzer.cpp first, as this is the jubo TU
ANALYZER_SOURCES=analyzer.cpp \
		 source_location.cpp \
		 script.cpp

ifeq ($(ASAN),)
	CPPFLAGS=$(BASICFLAGS) $(DEBUGFLAGS)
else
	CPPFLAGS=$(BASICFLAGS) $(ASANFLAGS)
endif

LDFLAGS=-L$(TCL)/unix -ltcl9.0 -lz  -lpthread -framework CoreFoundation

.PHONY: all clean

all: parse analyzer

parse: parse.cpp

analyzer: $(ANALYZER_SOURCES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f parse analyzer
	rm -rf parse.dSYM analyzer.dSYM


# vim: ts=4
