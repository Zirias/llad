CC := gcc

CFLAGS += -std=c99 -Wall -Wextra -Wformat=2 -Winit-self \
	  -Wdeclaration-after-statement -Wshadow -Wbad-function-cast \
	  -Wwrite-strings -Wconversion -Wlogical-op -pedantic -std=c99

CCDEP := $(CC) -MM

INSTALL := install

prefix := /usr/local

sbindir := $(prefix)/sbin
sysconfdir := $(prefix)/etc
localstatedir := $(prefix)/var
runstatedir := $(localstatedir)/run
docbasedir := $(prefix)/share/doc
docdir := $(docbasedir)/llad

VTAGS :=
V := 0

ifdef DEBUG
CFLAGS += -Werror -pedantic-errors -Wsuggest-attribute=pure \
	  -Wsuggest-attribute=const -Wsuggest-attribute=noreturn \
	  -Wsuggest-attribute=format -Wpadded \
	  -Wno-error=suggest-attribute=pure \
	  -Wno-error=suggest-attribute=const \
	  -Wno-error=suggest-attribute=noreturn \
	  -Wno-error=suggest-attribute=format \
	  -Wno-error=padded -DDEBUG -g3 -O0
VTAGS += [debug]
else
CFLAGS += -g0 -O3 -flto
LDFLAGS += -O3 -flto
VTAGS += [release]
endif

ifeq ($(V),1)
VCC :=
VDEP :=
VLD :=
VCCLD :=
VGEN :=
VGENT :=
VR :=
else
VCC = @echo "   [CC]   $@"
VDEP = @echo "   [DEP]  $@"
VLD = @echo "   [LD]   $@"
VCCLD = @echo "   [CCLD] $@"
VGEN = @echo "   [GEN]  $@"
VGENT = @echo "   [GEN]  $@: $(VTAGS)"
VR := @
endif

all: sbin/llad

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),distclean)
conf.mk:
	$(VGENT)
	$(VR)echo "C_CC :=$(CC)" >conf.mk
	$(VR)echo "C_DEBUG :=$(DEBUG)" >>conf.mk
	$(VR)echo "C_sysconfdir :=$(sysconfdir)" >>conf.mk
	$(VR)echo "C_runstatedir :=$(runstatedir)" >>conf.mk

-include conf.mk

ifneq ($(strip $(C_CC))_$(strip $(C_DEBUG))_$(strip $(C_sysconfdir))_$(strip $(C_runstatedir)),$(strip $(CC))_$(strip $(DEBUG))_$(strip $(sysconfdir))_$(strip $(runstatedir)))
.PHONY: conf.mk
endif
endif
endif

llad_DEFINES := -DSYSCONFDIR="\"$(sysconfdir)\"" \
	-DRUNSTATEDIR="\"$(runstatedir)\""
llad_OBJS := obj/llad.o obj/util.o obj/daemon.o obj/config.o obj/logfile.o \
    obj/watcher.o obj/action.o
llad_LIBS := -pthread -lpopt -lpcre

sbin/llad: $(llad_OBJS) | sbin
	$(VCCLD)
	$(VR)$(CC) $(LDFLAGS) -o $@ $^ $(llad_LIBS)

clean:
	rm -fr obj

distclean: clean
	rm -f conf.mk
	rm -fr sbin

obj:
	$(VR)mkdir -p obj

sbin:
	$(VR)mkdir -p sbin

strip: all
	$(VR)strip --strip-all sbin/llad

install: strip
	$(INSTALL) -d $(DESTDIR)$(sbindir)
	$(INSTALL) -d $(DESTDIR)$(docdir)/examples/command
	$(INSTALL) sbin/llad $(DESTDIR)$(sbindir)
	$(INSTALL) -m644 README.md $(DESTDIR)$(docdir)
	$(INSTALL) -m644 examples/llad.conf $(DESTDIR)$(docdir)/examples
	$(INSTALL) -m755 examples/command/* $(DESTDIR)$(docdir)/examples/command

obj/%.d: src/%.c Makefile conf.mk | obj
	$(VDEP)
	$(VR)$(CCDEP) -MT"$@ $(@:.d=.o)" -MF$@ \
		$(llad_DEFINES) $(CFLAGS) $<

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),distclean)
-include $(llad_OBJS:.o=.d)
endif
endif

obj/%.o: src/%.c Makefile conf.mk | obj
	$(VCC)
	$(VR)$(CC) $(llad_DEFINES) $(CFLAGS) -c -o $@ $<

.PHONY: all clean distclean strip install

