TCL ?= tcl
DEBUGFLAGS=-g -O0 -fno-omit-frame-pointer -Wall -Wextra
RELEASEFLAGS=-g -O2 -Wall -Wextra -Werror
ASANFLAGS=-fsanitize=address,undefined $(DEBUGFLAGS)

BASICFLAGS=-I$(TCL)/generic -I$(TCL)/unix -std=c++17 

# debug/release
TARGET ?= debug

# put analyzer.cpp first, as this is the jubo TU
ANALYZER_SOURCES=src/analyzer.cpp \
				 src/source_location.cpp \
				 src/script.cpp \
				 src/index.cpp

ifeq ($(TARGET),debug)
	ifeq ($(ASAN),)
		CPPFLAGS=$(BASICFLAGS) $(DEBUGFLAGS)
	else
		CPPFLAGS=$(BASICFLAGS) $(ASANFLAGS)
	endif
else
	CPPFLAGS=$(BASICFLAGS) $(RELEASEFLAGS)
endif

LDFLAGS=-L$(TCL)/unix -ltcl9.0 -lz  -lpthread -framework CoreFoundation

.PHONY: all clean test help

all: $(TARGET) $(TARGET)/parse $(TARGET)/analyzer

help:
	@echo "Developer build. See also cmake."
	@echo "--------------------------------"
	@echo ""
	@echo "make [all|clean|test] TARGET=(debug|release) [ASAN=1] - build/clean/test"
	@echo "make show_<var> - print the make variable 'var'"
	@echo ""
	@echo "Default TARGET is debug"
	@echo "Default recipe is all"
	@echo ""
	@echo "Builds into ./debug or ./release (depending on TARGET)"
	@echo ""

$(TARGET)/parse: src/parse.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $<

$(TARGET)/analyzer: $(ANALYZER_SOURCES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $<

$(TARGET):
	@if [ "$(TARGET)" = "release" ] || [ "$(TARGET)" = "debug" ]; then \
		mkdir -p $(TARGET); \
	else \
		echo "Invalid target $(TARGET)"; \
		false; \
	fi


clean:
	@echo Clean $(TARGET)/
	@rm -rf $(TARGET)/analyzer $(TARGET)/analyzer.dSYM
	@rm -rf $(TARGET)/parse $(TARGET)/parse.dSYM
	@if [ -d $(TARGET)/ ]; then rmdir $(TARGET); fi

test: $(TARGET)/analyzer
	$(TARGET)/analyzer --test

show_%:
	@echo ${$(@:show_%=%)}

# vim: ts=4
