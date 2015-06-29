CC := gcc

CFLAGS += -Wall -Werror -pedantic -std=c99 \
	  -Werror=implicit-int \
	  -Werror=implicit-function-declaration

CCDEP := $(CC) -MM

prefix := /usr/local

sbindir := $(prefix)/sbin
sysconfdir := $(prefix)/etc

VTAGS :=
V := 0

ifdef DEBUG
CFLAGS += -DDEBUG -g3 -O0
VTAGS += [debug]
else
CFLAGS += -g0 -O3
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

-include conf.mk

ifneq ($(strip $(C_CC))_$(strip $(C_DEBUG))_$(strip $(C_sysconfdir)),$(strip $(CC))_$(strip $(DEBUG))_$(strip $(sysconfdir)))
.PHONY: conf.mk
endif
endif
endif

llad_DEFINES := -DSYSCONFDIR="$(sysconfdir)"
llad_OBJS := obj/llad.o obj/util.o obj/daemon.o obj/config.o obj/logfile.o \
    obj/watcher.o obj/action.o
llad_LIBS := -pthread -lpopt -lpcre

sbin/llad: $(llad_OBJS) | sbin
	$(VCCLD)
	$(VR)$(CC) -o $@ $^ $(llad_LIBS)

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

.PHONY: all clean distclean strip

