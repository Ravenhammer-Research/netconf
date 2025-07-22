# FreeBSD Network Configuration Tool

A FreeBSD application that provides ifconfig and route functionality using a NETCONF-style command syntax. The application consists of a client (`net`) and server (`netd`) that communicate via UNIX sockets.

## Features

- NETCONF-style command syntax
- Interactive and one-shot command modes
- Direct ioctl and routing socket operations (no system() calls)
- Support for IPv4 and IPv6
- FIB (routing table) support
- Tabular output for show commands

## Prerequisites

- FreeBSD system
- libnetconf2 development libraries
- libxml2 development libraries
- OpenSSL development libraries

Install dependencies:
```bash
pkg install libnetconf2 libxml2 openssl
```

## Building

```bash
make
```

This will create:
- `bin/net` - The client application
- `bin/netd` - The server daemon

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
net> set interface ethernet em0 inet addr 192.168.1.100/24
net> set route protocols static inet 0.0.0.0/0 192.168.1.1
net> commit
net> save
net> quit
```

#### One-shot Mode

```bash
net show interface
net set interface ethernet em0 inet addr 192.168.1.100/24
net set route protocols static inet 0.0.0.0/0 192.168.1.1
net commit
net save
```

## Command Syntax

### Interface Commands

```
show interface                                    # Show all interfaces
set interface ethernet <if> inet addr <addr>/<prefix> [fib N]
set interface ethernet <if> inet6 addr <addr>/<prefix> [fib N]
```

### Route Commands

```
set route protocols static [fib N] inet <dest> <gw>
set route protocols static [fib N] inet6 <dest> <gw>
delete route protocols static [fib N]            # Delete all static routes
```

### System Commands

```
commit                                           # Apply queued changes
save                                             # Persist configuration
```

## Examples

### Configure Interface

```bash
# Set IPv4 address on em0
net set interface ethernet em0 inet addr 192.168.1.100/24

# Set IPv6 address on em0
net set interface ethernet em0 inet6 addr 2001:db8::100/64

# Set address with specific FIB
net set interface ethernet em0 inet addr 10.0.0.100/24 fib 1
```

### Configure Routes

```bash
# Add default route
net set route protocols static inet 0.0.0.0/0 192.168.1.1

# Add specific route
net set route protocols static inet 10.0.0.0/24 192.168.1.254

# Add IPv6 route
net set route protocols static inet6 2001:db8::/64 2001:db8::1

# Add route with specific FIB
net set route protocols static fib 1 inet 0.0.0.0/0 10.0.0.1
```

### Show Information

```bash
# Show all interfaces
net show interface

# Show routing table
net show route
```

## Architecture

The application uses a client-server architecture:

- **Client (`net`)**: Parses commands and communicates with the server
- **Server (`netd`)**: Runs with root privileges and performs actual system operations

Communication is via UNIX domain sockets for security and performance.

## Implementation Details

- Uses direct ioctl calls for interface configuration (no system() calls to ifconfig)
- Uses routing sockets for route management (no system() calls to route)
- Supports both IPv4 and IPv6 addressing
- Implements FIB (routing table) support
- Provides tabular output for show commands

## Future Enhancements

- Full NETCONF protocol support using libnetconf2
- Configuration validation
- Transaction support
- Configuration rollback
- Support for additional interface types
- VLAN support
- Bridge configuration
- Firewall rule management

## License

This project is provided as-is for educational and development purposes. 