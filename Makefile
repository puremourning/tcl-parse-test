TCL ?= tcl
TCL_VERSION=9.0
DEBUGFLAGS=-g -O0 -fno-omit-frame-pointer -Wall -Wextra
RELEASEFLAGS=-g -O2 -Wall -Wextra -Werror
ASANFLAGS=-fsanitize=address,undefined -fsanitize-recover=address

# debug/release
TARGET ?= debug
ARCH ?= x86_64

BUILD_DEST = $(TARGET)-$(ARCH)
BIN_DIR = $(BUILD_DEST)/bin

BASICFLAGS=-I$(BUILD_DEST)/include -I$(TCL)/generic -I$(TCL)/unix -std=c++17 -arch $(ARCH)

# put analyzer.cpp first, as this is the jubo TU
ANALYZER_SOURCES=src/analyzer.cpp \
				 src/source_location.cpp \
				 src/script.cpp \
				 src/index.cpp

BUILD_INF=Makefile

CPPFLAGS=$(BASICFLAGS)
ifeq ($(TARGET),debug)
	ifeq ($(NOASAN),)
		ASAN=1
	endif

	CPPFLAGS+=$(DEBUGFLAGS)
else
	CPPFLAGS+=$(RELEASEFLAGS)
endif

ifeq ($(ASAN),1)
	CPPFLAGS+=$(ASANFLAGS)
endif

LDFLAGS=-L$(BUILD_DEST)/lib -ltcl$(TCL_VERSION) -lz  -lpthread -framework CoreFoundation

.PHONY: all clean test help

all: $(BUILD_DEST) $(BIN_DIR)/parse $(BIN_DIR)/analyzer

help:
	@echo "Developer build. See also cmake."
	@echo "--------------------------------"
	@echo ""
	@echo "make [all|clean|test] TARGET=(debug|release) ARCH=(x86_64|arm64) [ASAN=1] - build/clean/test"
	@echo "make show_<var> - print the make variable 'var'"
	@echo ""
	@echo "Default TARGET is debug"
	@echo "Default ARCH is x86_64"
	@echo "Default recipe is all"
	@echo ""
	@echo "Builds into ./TARGET-ARCH, e.g. debug-arm64 or release-x86_64, etc."
	@echo ""

TCL_SOURCES=$(shell ls -1 $(TCL)/unix/*.c)
TCL_LIB=$(BUILD_DEST)/lib/libtcl$(TCL_VERSION).a

$(TCL_LIB): $(BUILD_DEST) $(TCL_SOURCES)
	@cd $(TCL)/unix && \
		env CFLAGS="-arch $(ARCH)" \
		./configure --prefix $(CURDIR)/$(BUILD_DEST) \
				    --disable-shared \
					--host $(ARCH)-apple-darwin \
					--disable-framework && \
		$(MAKE) clean && \
		$(MAKE) && \
		$(MAKE) install

$(BIN_DIR)/parse: src/parse.cpp $(BUILD_INF) $(TCL_LIB)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $<

$(BIN_DIR)/analyzer: $(ANALYZER_SOURCES) $(BUILD_INF) $(TCL_LIB)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD_DEST):
	@if [ "$(ARCH)" != "arm64" ] && [ "$(ARCH)" != "x86_64" ]; then\
		echo "Invalid arch $(ARCH)"; \
		false; \
	elif [ "$(TARGET)" != "release" ] && [ "$(TARGET)" != "debug" ]; then \
		echo "Invalid target $(TARGET)"; \
		false; \
	else \
		mkdir -p $(TARGET)-$(ARCH); \
		mkdir -p $(BIN_DIR); \
	fi


clean:
	@echo Clean $(BUILD_DEST)/
	@rm -rf $(BUILD_DEST)
	@cd $(TCL)/unix && $(MAKE) clean

test: $(BIN_DIR)/analyzer
	$(BIN_DIR)/analyzer --test

show_%:
	@echo ${$(@:show_%=%)}

# vim: ts=4
