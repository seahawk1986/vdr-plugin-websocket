#
# Makefile for a Video Disk Recorder plugin
#
# $Id$

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.

PLUGIN = websocket

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep -E 'static const char \*VERSION\s*=' $(PLUGIN).c | cut -d '"' -f 2)

### The directory environment:

# Use package data if installed...otherwise assume we're under the VDR source directory:
PKG_CONFIG ?= pkg-config
PKGCFG = $(if $(VDRDIR),$(shell $(PKG_CONFIG) --variable=$(1) $(VDRDIR)/vdr.pc),$(shell PKG_CONFIG_PATH="$$PKG_CONFIG_PATH:../../.." $(PKG_CONFIG) --variable=$(1) vdr))
LIBDIR = $(call PKGCFG,libdir)
LOCDIR = $(call PKGCFG,locdir)
PLGCFG = $(call PKGCFG,plgcfg)
#
TMPDIR ?= /tmp

### The compiler options:

export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)

CXXFLAGS += -std=c++17 -fPIC
CFLAGS   += -fPIC

### The version number of VDR's plugin API:

APIVERSION = $(call PKGCFG,apiversion)

### Allow user defined options to overwrite defaults:

-include $(PLGCFG)

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The name of the shared object file:

SOFILE = libvdr-$(PLUGIN).so

### Includes and Defines (add further entries here):
MONGOOSE_DIR = mongoose

INCLUDES += -I$(MONGOOSE_DIR) -I/usr/include

DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"' -DMG_ENABLE_LOG=0

### The object files (add further files here):


OBJS = $(MONGOOSE_DIR)/mongoose.o statusmonitor.o websocketthread.o $(PLUGIN).o

### The main target:

all: $(SOFILE) i18n

### Implicit rules:

BASE_FLAGS = $(CPPFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP

### Implicit rules:

BASE_FLAGS = $(CPPFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP

%.o: %.cpp
	@echo CXX $@
	$(Q)$(CXX) $(CXXFLAGS) $(BASE_FLAGS) -c -o $@ $<

%.o: %.c
	@echo CC $@
	$(Q)$(CC) $(CFLAGS) $(BASE_FLAGS) -c -o $@ $<

$(PLUGIN).o: $(wildcard $(PLUGIN).c) $(wildcard $(PLUGIN).cpp)
	@echo CXX $@
	$(Q)$(CXX) $(CXXFLAGS) $(BASE_FLAGS) -x c++ -c -o $@ $<


### Dependencies:

MAKEDEP = $(CXX) -MM -MG

-include $(OBJS:.o=.d)

### Check for dependencies:

JSON_HEADER = nlohmann/json.hpp
JSON_CHECK = $(shell echo '\#include <$(JSON_HEADER)>' | $(CXX) $(CXXFLAGS) $(INCLUDES) -E - >/dev/null 2>&1 && echo "ok" || echo "missing")

ifeq ($(JSON_CHECK),missing)
  $(error "FEHLER: $(JSON_HEADER) nicht gefunden! Bitte 'nlohmann-json3-dev' (Debian/Ubuntu) oder 'nlohmann-json' (Arch/Fedora) installieren.")
endif

### Internationalization (I18N):

PODIR     = po
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmo    = $(addsuffix .mo, $(foreach file, $(I18Npo), $(basename $(file))))
I18Nmsgs  = $(addprefix $(DESTDIR)$(LOCDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	@echo MO $@
	$(Q)msgfmt -c -o $@ $<

I18N_SOURCES = $(wildcard *.c *.cpp *.h *.hpp)

$(I18Npot): $(I18N_SOURCES)
	@echo GT $@
	$(Q)xgettext -C -cTRANSLATORS --no-wrap --no-location \
		-k -ktr -ktrNOOP --package-name=vdr-$(PLUGIN) \
		--package-version=$(VERSION) \
		--msgid-bugs-address='<see README>' -o $@ $(I18N_SOURCES)

%.po: $(I18Npot)
	@echo PO $@
	$(Q)msgmerge -U --no-wrap --no-location --backup=none -q -N $@ $<
	@touch $@

$(I18Nmsgs): $(DESTDIR)$(LOCDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	install -D -m644 $< $@

.PHONY: i18n
i18n: $(I18Nmo) $(I18Npot)

### Targets:

$(SOFILE): $(OBJS)
	@echo LD $@
	$(Q)$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared $(OBJS) -o $@

install-lib: $(SOFILE)
	install -D $^ $(DESTDIR)$(LIBDIR)/$^.$(APIVERSION)

install: install-lib install-i18n

dist: $(I18Npo) clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) --exclude debian --exclude CVS --exclude .svn --exclude .hg --exclude .git $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~
	@-find . -name "*.d" -type f -delete
