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
          src/alias.c \
          src/title.c

HEADERS = src/shell.h src/parser.h

TARGET_BASENAME = catshellx
BUILDDIR = Build
PKGDIR = Packages

TARGET = $(BUILDDIR)/$(TARGET_BASENAME)
PTYTEST = $(BUILDDIR)/ptytest
CATCONFIG = $(BUILDDIR)/cat_config

PREFIX ?= /usr/local
DESTDIR ?=
BINDIR = $(DESTDIR)$(PREFIX)/bin

PKG_ID ?= com.regalf.catshellx
PKG_VERSION ?= 2.0
PKG = $(PKGDIR)/CatShellX-$(PKG_VERSION).pkg
DMG = $(PKGDIR)/CatShellX-$(PKG_VERSION).dmg
PKGMAKER = /Developer/Applications/Utilities/PackageMaker.app/Contents/MacOS/PackageMaker

.PHONY: all clean ssh test install pkg dmg

all: $(TARGET) $(PTYTEST) $(CATCONFIG)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES)
	@echo "Build complete: $(TARGET)"

$(PTYTEST): tests/ptytest.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@echo "Build complete: $(PTYTEST)"

$(CATCONFIG): tools/cat_config.c src/util.c $(HEADERS)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tools/cat_config.c src/util.c
	@echo "Build complete: $(CATCONFIG)"

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
	rm -rf $(BUILDDIR) $(PKGDIR) tests/ptytest
	@echo "Clean."

ssh:
	ssh 192.168.1.9 "cd /Users/regaldragoon200/Desktop/sshserver/CatShellX && make"

install: $(TARGET) $(CATCONFIG)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET_BASENAME)
	install -m 755 $(CATCONFIG) $(BINDIR)/cat_config
	@echo "Installed $(TARGET_BASENAME) and cat_config to $(BINDIR)"
	@echo "Note: /usr/local/bin is not in the default Tiger PATH;"
	@echo "add 'export PATH=\"/usr/local/bin:\$$PATH\"' to ~/.bash_profile."

pkg: $(TARGET) $(CATCONFIG)
	rm -rf /tmp/csx_pkgroot /tmp/csx_Info.plist
	mkdir -p /tmp/csx_pkgroot/usr/local/bin
	cp $(TARGET) /tmp/csx_pkgroot/usr/local/bin/$(TARGET_BASENAME)
	cp $(CATCONFIG) /tmp/csx_pkgroot/usr/local/bin/cat_config
	printf '%s\n' \
	  '<?xml version="1.0" encoding="UTF-8"?>' \
	  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	  '<plist version="1.0">' \
	  '<dict>' \
	  '  <key>CFBundleIdentifier</key><string>$(PKG_ID)</string>' \
	  '  <key>CFBundleName</key><string>CatShellX</string>' \
	  '  <key>CFBundleShortVersionString</key><string>$(PKG_VERSION)</string>' \
	  '  <key>CFBundleVersion</key><string>$(PKG_VERSION)</string>' \
	  '  <key>IFPkgFlagDefaultLocation</key><string>/</string>' \
	  '</dict>' \
	  '</plist>' > /tmp/csx_Info.plist
	@mkdir -p $(PKGDIR)
	$(PKGMAKER) -build -p $(PKG) -f /tmp/csx_pkgroot -i /tmp/csx_Info.plist
	@echo "Built $(PKG)"

dmg: pkg
	@mkdir -p $(PKGDIR)
	hdiutil create -srcfolder $(PKG) -volname CatShellX -ov $(DMG) >/dev/null
	@echo "Built $(DMG)"
