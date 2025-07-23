# FreeBSD Network Configuration Tool

A FreeBSD application that provides ifconfig and route functionality using a NETCONF-style command syntax. The application consists of a client (`net`) and server (`netd`) that communicate via UNIX sockets.

## Features

- NETCONF-style command syntax
- Interactive and one-shot command modes
- Interface configuration
- Route management
- Configuration persistence
- YANG model integration

## Prerequisites

- FreeBSD system
- libnetconf2 development libraries
- libyang development libraries

Install dependencies:
```bash
pkg install libnetconf2 libyang
```

## Building

```bash
make
```

This will create:
- `net` - The client application
- `netd` - The server daemon

## Installation

```bash
sudo make install
```

This installs:
- `net` to `/usr/local/bin/`
- `netd` to `/usr/local/sbin/`

## Usage

### Starting the Server

First, start the netd server (requires root privileges):

```bash
sudo netd
```

The server will listen on `/var/run/netd.sock`.

### Using the Client

#### Interactive Mode

```bash
net
```

This starts an interactive session where you can enter commands:

```
net> show interface
net> set interface tap tap5 inet address 192.168.0.0/31
net> set route protocol static inet default 192.168.0.1
net> commit
net> save
net> quit
```

#### One-shot Mode

```bash
net show interface
net set interface tap tap5 inet address 192.168.0.0/31
net set route protocol static inet default 192.168.0.1
net commit
net save
```

## Command Syntax

### Interface Commands

```
show interface                                    # Show all interfaces
show interface <filter>                           # Show filtered interfaces
set interface <type> <name> inet address <addr>/<prefix>
set interface <type> <name> inet6 address <addr>/<prefix>
```

### Route Commands

```
set route protocol static inet <dest> <gw>
set route protocol static inet6 <dest> <gw>
```

### System Commands

```
commit                                           # Apply queued changes
save                                             # Persist configuration
help                                             # Show help
quit                                             # Exit interactive mode
```

## Examples

### Configure Interface

```bash
# Set IPv4 address on tap interface
net set interface tap tap5 inet address 192.168.0.0/31

# Set IPv6 address on tap interface
net set interface tap tap6 inet6 address 2001:db8::100/64

# Show specific interface
net show interface tap5
```

### Configure Routes

```bash
# Add default route
net set route protocol static inet default 192.168.0.1

# Add specific route
net set route protocol static inet 10.0.0.0/24 192.168.0.254

# Add IPv6 route
net set route protocol static inet6 2001:db8::/64 2001:db8::1


```

### Show Information

```bash
# Show all interfaces
net show interface

# Show tap interfaces only
net show interface tap

## License

Copyright (c) 2025 Paige Thompson / Raven Hammer Research (paige@paige.bio)

This project is licensed under the BSD 3-Clause License. See the [LICENSE](LICENSE) file for details.
