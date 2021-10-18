TCL ?= tcl
DEBUGFLAGS=-g -O0 -fno-omit-frame-pointer -Wall -Wextra
RELEASEFLAGS=-g -O2 -Wall -Wextra -Werror
ASANFLAGS=-fsanitize=address,undefined -fsanitize-recover=address

# debug/release
TARGET ?= debug
ARCH ?= x86_64

BUILD_DEST = $(TARGET)-$(ARCH)

BASICFLAGS=-I$(TCL)/generic -I$(TCL)/unix -std=c++17 -arch $(ARCH)

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

LDFLAGS=-L$(TCL)/unix -ltcl9.0 -lz  -lpthread -framework CoreFoundation

.PHONY: all clean test help

all: $(BUILD_DEST) $(BUILD_DEST)/parse $(BUILD_DEST)/analyzer

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

$(BUILD_DEST)/parse: src/parse.cpp $(BUILD_INF)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD_DEST)/analyzer: $(ANALYZER_SOURCES) $(BUILD_INF)
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
	fi


clean:
	@echo Clean $(BUILD_DEST)/
	@rm -rf $(BUILD_DEST)/analyzer $(BUILD_DEST)/analyzer.dSYM
	@rm -rf $(BUILD_DEST)/parse $(BUILD_DEST)/parse.dSYM
	@if [ -d $(BUILD_DEST)/ ]; then rmdir $(BUILD_DEST); fi

test: $(BUILD_DEST)/analyzer
	$(BUILD_DEST)/analyzer --test

show_%:
	@echo ${$(@:show_%=%)}

# vim: ts=4
