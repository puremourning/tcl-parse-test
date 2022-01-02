TCL ?= vendor/tcl
ASIO ?= vendor/asio
JSON ?= vendor/nlohmann

TCL_VERSION=9.0
DEBUGFLAGS=-g -O0 -fno-omit-frame-pointer -Wall -Wextra \
		   -DASIO_ENABLE_HANDLER_TRACKING=ON
RELEASEFLAGS=-g -O2 -Wall -Wextra -Werror
ASANFLAGS=-fsanitize=address,undefined -fsanitize-recover=address
TSANFLAGS=-fsanitize=thread

# debug/release
TARGET ?= debug
PLATFORM = $(shell uname)
ARCH ?= $(shell uname -m)

ifeq (${PLATFORM},Darwin)
	# use the latest and greatest llvm/libc++
	BREW=$(shell brew --prefix llvm)
	CC=$(BREW)/bin/clang
	CXX=$(BREW)/bin/clang++
	LD=$(BREW)/bin/lld

	# We have to teach ASIO that this version of llvm has c++20-compilant
	# std::invoke_result (i.e. there is no std::result_of)
	BASICFLAGS+=-DASIO_HAS_STD_INVOKE_RESULT
endif

BUILD_DEST = $(TARGET)-$(PLATFORM)-$(ARCH)
BIN_DIR = $(BUILD_DEST)/bin

BASICFLAGS+=-isystem $(BUILD_DEST)/include \
		    -isystem $(TCL)/generic -isystem $(TCL)/unix \
		    -isystem $(ASIO)/include \
		    -isystem $(JSON) \
		    -I$(CURDIR)/src \
		    -std=c++20

LIBANALYZER_SOURCES= src/analyzer/source_location.cpp \
					 src/analyzer/script.cpp \
					 src/analyzer/index.cpp \
					 src/analyzer/db.cpp

# put analyzer.cpp first, as this is the jubo TU
ANALYZER_SOURCES=src/analyzer.cpp \
				 $(LIBANALYZER_SOURCES)

# put server.cpp first, as this is the jubo TU
SERVER_SOURCES=src/server.cpp \
			   src/lsp/types.cpp \
			   src/lsp/comms.cpp \
			   src/lsp/server.hpp \
			   src/lsp/handlers.cpp \
			   src/lsp/parse_manager.cpp \
			   $(LIBANALYZER_SOURCES)

BUILD_INF=Makefile

CPPFLAGS=$(BASICFLAGS)
ifeq ($(TARGET),debug)
	ifeq ($(NOASAN),)
		ifeq ($(TSAN),)
			ASAN=1
		endif
	endif

	CPPFLAGS+=$(DEBUGFLAGS)
else
	CPPFLAGS+=$(RELEASEFLAGS)
endif

ifeq ($(ASAN),1)
	CPPFLAGS+=$(ASANFLAGS)
endif

ifeq ($(TSAN),1)
	CPPFLAGS+=$(TSANFLAGS)
endif

LDFLAGS=-L$(BUILD_DEST)/lib -ltcl$(TCL_VERSION) -lz  -lpthread

.PHONY: all help clean distclean test compdb

all: $(BUILD_DEST)  $(BIN_DIR)/analyzer $(BIN_DIR)/server

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

distclean: clean
	@rm -rf $(BUILD_DEST)
	@cd $(TCL)/unix && ( [ ! -f Makefile ] || $(MAKE) distclean )
	@rm -f compile_commands.json
	@rm -rf .cache

test: $(BIN_DIR)/analyzer $(BIN_DIR)/server
	$(BIN_DIR)/analyzer --test
	$(BIN_DIR)/analyzer --file test/test.tcl
	$(BIN_DIR)/analyzer --file test/simple.tcl
	$(BIN_DIR)/server <test/lsp/input >test/lsp/cout
	$(BIN_DIR)/server <test/lsp/input2 >test/lsp/cout

show_%:
	@echo ${$(@:show_%=%)}

compdb: distclean
	@compiledb make MAKEFLAGS=$(MAKEFLAGS)

# vim: ts=4
