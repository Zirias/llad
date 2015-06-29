# llad

Linux Log Action Daemon (watch logfiles and take actions on patterns)

The Linux Log Action Daemon is a little tool allowing to use log files as a
source of events. Based on a config file, llad watches several logfiles and
executes some custom action as soon as a pattern matches a new log line.

## The idea

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

## Features

- watch any amount of logfiles

- configurable pairs of patterns to look for as perl compatible regular
  expressions and commands to execute when the pattern matches

## Installation

### Prerequisites

In order to build llad, you need the following libraries:

- libpopt

- libpcre

### Quick installation

Use the following commands:

	make
	make install

### Build configuration

The Makefile (GNU make) understands the following options for "make":

- DEBUG=1: create a build with debugging info

- prefix=<path> (default: /usr/local) the prefix for the running installation

- sysconfdir=<path> (default: <prefix>/etc) the location of configuration
  files. llad creates its own subdirectory "llad" and commands for executing
  are in <sysconfdir>/llad/command by default.

- localstatedir=<path> (default: <prefix>/var) -- this is used for the default
  location of llad's pidfile (<localstatedir>/run/llad.pid).

### Install configuration

"make install" should receive the SAME options given to "make". Furthermore,
the option "DESTDIR" is supported. This is mainly interesting for package
builders -- DESTDIR will be prepended to any path while installing.

## Documentation

See the files in "examples".

