# llad

### Linux Log Action Daemon (watch logfiles and take actions on patterns)

The Linux Log Action Daemon is a little tool allowing to use log files as a
source of events. Based on a config file, llad watches several logfiles and
executes some custom actions as soon as a pattern matches a new log line.

The patterns are given as perl compatible regular expressions. The matching
part of a log line, as well as any matchings for "capture groups", are fed to
the executed command as commandline arguments. Output of the command is
captured and logged to the "daemon" log facility.

A major design goal of llad is robustness -- all executed actions are monitored
and cancelled, if they exceed some configurable timeouts.

All llad code is released under the terms of the 2-clause BSD license.

### Current state

llad is ready for production use according to my own tests, but not yet tested
in the field. Therefore, there's no "release" yet. The files in debian/ will
create a Debian package with version number 0.9 -- the first official release
will be 1.0.

## Installation

### Prerequisites

In order to build llad, you need the following libraries:

- libpopt

- libpcre

For running it, you need a Linux kernel (>= 2.6.36) that provides the inotify
API.

### Quick installation

Use the following commands:

	make
	make install

### Build configuration

The Makefile (GNU make) understands the following options for "make":

- DEBUG=1: create a build with debugging info

- prefix={path} (default: /usr/local) the prefix for the running installation

- sysconfdir={path} (default: {prefix}/etc) the location of configuration
  files. llad creates its own subdirectory "llad" and commands for executing
  are in {sysconfdir}/llad/command by default.

- localstatedir={path} (default: {prefix}/var) -- this is used for the default
  location of llad's pidfile ({localstatedir}/run/llad.pid).

### Install configuration

"make install" should receive the SAME options given to "make". Furthermore,
the option "DESTDIR" is supported. This is mainly interesting for package
builders -- DESTDIR will be prepended to any path while installing.

## Further documentation

See the files in "examples".

## Why? The idea

I have a Linux based router/firewall and once in a while (every few months)
the NIC driver goes crazy. Networking has to be restarted manually to get it
back to work. This is annoying, need a serial terminal for this, so I came up
with a quick and dirty daemon created in perl that just watches
/var/log/messages for NETDEV WATCHDOG entries and restarts the networking
automatically.

Well, a nice hack, but I wanted a more solid solution, so why not create a
daemon for watching any logfile you want (or, multiple logfiles at a time),
looking for user-defined patterns and taking actions given in a configuration
file. This is what llad does.

