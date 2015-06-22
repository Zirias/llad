# llad

Linux Log Action Daemon (watch logfiles and take actions on patterns)

This is work in progress. So far, the tool doesn't do anything useful. This
README will be updated as soon as llad is actually usable.

## The idea

I have a Linux based router/firewall and once in a while (every few months)
the NIC driver goes crazy. Networking has to be restarted manually to get it
back to work. This is annoying, need a serial terminal for this, so I came up
with a quick and dirty daemon created in perl that just watches
/var/log/messages for NETDEV WATCHDOG entries and restarts the networking
automatically.

Well, a nice hack, but I want a more solid solution, so why not create a
daemon for watching any logfile you want (or, multiple logfiles at a time),
looking for user-defined patterns and taking actions given in a configuration
file. This is what llad should do in the future.

## Planned features

- watch any amount of logfiles

- configurable pairs of patterns to look for as perl compatible regular
expressions and commands to execute when the pattern matches

## Config file sketchup

  [/var/log/messages]

  nicwatch = {
    pattern = "NETDEV\s+WATCHDOG:\s+eth0"
    command = "restart-network.sh"
  }

