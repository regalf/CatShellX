# CatShellX - fish-like shell for Mac OS X 10.4 Tiger
# C (gnu99) / GCC 4.0.1 / GNU Make 3.80
# Target: universal PowerPC/i386

CC = gcc
SDK = /Developer/SDKs/MacOSX10.4u.sdk

ARCH_FLAGS = -arch ppc -arch i386
SYSROOT = -isysroot $(SDK)
MIN_VER = -mmacosx-version-min=10.4

CFLAGS = $(ARCH_FLAGS) $(SYSROOT) $(MIN_VER) \
         -std=gnu99 \
         -g -O0 -Wall -Wno-unused-parameter \
         -I src

LDFLAGS = $(ARCH_FLAGS) $(SYSROOT) $(MIN_VER)

SOURCES = src/main.c \
          src/util.c \
          src/execute.c \
          src/parser.c \
          src/builtins.c \
          src/history.c \
          src/line_editor.c \
          src/prompt.c \
          src/suggest.c \
          src/completion.c \
          src/expand.c \
          src/highlight.c \
          src/vars.c \
          src/alias.c

TARGET = catshellx
PTYTEST = tests/ptytest

.PHONY: all clean ssh test

all: $(TARGET) $(PTYTEST)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES)
	@echo "Build complete: $(TARGET)"

$(PTYTEST): tests/ptytest.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@echo "Build complete: $(PTYTEST)"

test: $(TARGET) $(PTYTEST)
	mkdir -p /tmp/csx_home
	./$(PTYTEST) tests/edit1.txt
	./$(PTYTEST) tests/editor2.txt
	./$(PTYTEST) tests/suggest1.txt
	./$(PTYTEST) tests/complete1.txt
	./$(PTYTEST) tests/expand1.txt
	./$(PTYTEST) tests/jobs1.txt
	cp tests/testrc /tmp/csx_home/.catshellxrc
	cp tests/sourcerc /tmp/csx_home/sourcerc
	./$(PTYTEST) tests/config1.txt
	rm -f /tmp/csx_home/.catshellxrc /tmp/csx_home/sourcerc

clean:
	rm -f $(TARGET) src/*.o
	@echo "Clean."

ssh:
	ssh 192.168.1.9 "cd /Users/regaldragoon200/Desktop/sshserver/CatShellX && make"
