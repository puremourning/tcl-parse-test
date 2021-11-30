TCL ?= vendor/tcl
ASIO ?= vendor/asio
JSON ?= vendor/nlohmann

TCL_VERSION=9.0
DEBUGFLAGS=-g -O0 -fno-omit-frame-pointer -Wall -Wextra
RELEASEFLAGS=-g -O2 -Wall -Wextra -Werror
ASANFLAGS=-fsanitize=address,undefined -fsanitize-recover=address

# debug/release
TARGET ?= debug
PLATFORM = $(shell uname)
ARCH ?= $(shell uname -m)

BUILD_DEST = $(TARGET)-$(PLATFORM)-$(ARCH)
BIN_DIR = $(BUILD_DEST)/bin

BASICFLAGS=-isystem $(BUILD_DEST)/include \
		   -isystem $(TCL)/generic -isystem $(TCL)/unix \
		   -isystem $(ASIO)/include \
		   -isystem $(JSON) \
		   -std=c++20

# put analyzer.cpp first, as this is the jubo TU
ANALYZER_SOURCES=src/analyzer.cpp \
				 src/source_location.cpp \
				 src/script.cpp \
				 src/index.cpp \
				 src/db.cpp

# put server.cpp first, as this is the jubo TU
SERVER_SOURCES=src/server.cpp \
			   src/lsp/types.cpp \
			   src/lsp/comms.cpp \
			   src/lsp/server.hpp \
			   src/lsp/handlers.cpp

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

LDFLAGS=-L$(BUILD_DEST)/lib -ltcl$(TCL_VERSION) -lz  -lpthread

.PHONY: all clean test help

all: $(BUILD_DEST) $(BIN_DIR)/parse $(BIN_DIR)/analyzer $(BIN_DIR)/server

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
	@echo "Builds into ./TARGET-PLATFORM-ARCH, e.g. debug-Darwin-arm64 or release-Darwin-x86_64, etc."
	@echo ""

TCL_SOURCES=$(shell ls -1 $(TCL)/unix/*.c)
TCL_LIB=$(BUILD_DEST)/lib/libtcl$(TCL_VERSION).a

ifeq (Darwin,${PLATFORM})

LDFLAGS+=-framework CoreFoundation
ARCHFLAGS=-arch $(ARCH)
CPPFLAGS+=$(ARCHFLAGS)

$(TCL_LIB): $(BUILD_DEST) $(TCL_SOURCES)
	@cd $(TCL)/unix && \
		env CFLAGS="$(ARCHFLAGS)" \
		./configure --prefix $(CURDIR)/$(BUILD_DEST) \
					--disable-shared \
					--host $(ARCH)-apple-darwin \
					--disable-framework && \
		$(MAKE) clean && \
		$(MAKE) && \
		$(MAKE) install-binaries

else

CPPFLAGS += -Wno-missing-field-initializers -fuse-ld=gold
LDFLAGS+=-ldl

$(TCL_LIB): $(BUILD_DEST) $(TCL_SOURCES)
	@cd $(TCL)/unix && \
		./configure --prefix $(CURDIR)/$(BUILD_DEST) \
					--disable-shared \
					--disable-framework && \
		$(MAKE) clean && \
		$(MAKE) && \
		$(MAKE) install-binaries

endif

$(BIN_DIR)/parse: src/parse.cpp $(BUILD_INF) $(TCL_LIB)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR)/analyzer: $(ANALYZER_SOURCES) $(BUILD_INF) $(TCL_LIB)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR)/server: $(SERVER_SOURCES) $(BUILD_INF) $(TCL_LIB)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

$(BUILD_DEST):
	@if [ "$(ARCH)" != "arm64" ] && [ "$(ARCH)" != "x86_64" ]; then\
		echo "Invalid arch $(ARCH)"; \
		false; \
	elif [ "$(TARGET)" != "release" ] && [ "$(TARGET)" != "debug" ]; then \
		echo "Invalid target $(TARGET)"; \
		false; \
	else \
		mkdir -p $(BUILD_DEST); \
		mkdir -p $(BIN_DIR); \
	fi


clean:
	@echo Clean $(BUILD_DEST)/
	rm -f $(BIN_DIR)/analyzer
	rm -f $(BIN_DIR)/server
	rm -f $(BIN_DIR)/parse

distclean: clean
	@rm -rf $(BUILD_DEST)
	@cd $(TCL)/unix && $(MAKE) distclean

test: $(BIN_DIR)/analyzer $(BIN_DIR)/server
	$(BIN_DIR)/analyzer --test
	$(BIN_DIR)/analyzer --file test/test.tcl
	$(BIN_DIR)/analyzer --file test/simple.tcl
	$(BIN_DIR)/server < test/lsp/input 2> test/lsp/cerr

show_%:
	@echo ${$(@:show_%=%)}

# vim: ts=4
