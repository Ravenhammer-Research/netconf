# FreeBSD Network Configuration Tool

A FreeBSD application that provides ifconfig and route functionality using a NETCONF-style command syntax. The application consists of a client (`net`) and server (`netd`) that communicate via UNIX sockets.

## Features

- NETCONF-style command syntax
- Interactive and one-shot command modes
- Interface configuration
- Route management
- Tabular output for show commands
- Support for tap/tun interface creation
- Configuration persistence
- YANG model integration

## Prerequisites

- FreeBSD system
- libnetconf2 development libraries
- libyang development libraries
- OpenSSL development libraries
- libssh development libraries

Install dependencies:
```bash
pkg install libnetconf2 libyang openssl libssh
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
show route                                        # Show routing table
show route fib <n>                                # Show routes for specific FIB
set route protocol static inet <dest> <gw>
set route protocol static inet6 <dest> <gw>
delete route static                               # Delete all static routes
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

# Show routes for specific FIB
net show route fib 1
```

### Show Information

```bash
# Show all interfaces
net show interface

# Show tap interfaces only
net show interface tap

# Show routing table
net show route

# Show routes for FIB 1
net show route fib 1
```

## Recent Improvements

### Interface Configuration
- Fixed interface address setting using system calls to ifconfig
- Added support for tap/tun interface creation
- Improved address parsing with CIDR notation
- Added netmask display in interface listings

### Command Parsing
- Rewrote command parser with lexer-based approach
- Added structured command and target definitions
- Improved error handling and validation
- Fixed parsing of interface type and name

### Display Improvements
- Added IPv4 address netmask display (e.g., `192.168.0.0/31`)
- Improved tabular output formatting
- Added FIB value detection and display
- Enhanced interface information gathering

### Configuration Management
- Implemented configuration saving and loading
- Added YANG model integration
- Improved NETCONF protocol support
- Enhanced error handling and debugging

## Architecture

The application uses a client-server architecture:

- **Client (`net`)**: Parses commands and communicates with the server
- **Server (`netd`)**: Runs with root privileges and performs actual system operations

Communication is via UNIX domain sockets for security and performance.

## Implementation Details

- Uses system calls to ifconfig for interface configuration
- Uses routing sockets for route management
- Supports both IPv4 and IPv6 addressing
- Provides tabular output for show commands
- Includes YANG model validation
- Supports configuration persistence

## Debugging

The server includes extensive debug output that can be enabled by running:

```bash
sudo netd
```

Debug output shows:
- Command parsing steps
- Interface configuration operations
- Address parsing and validation
- System call results and error codes

## Future Enhancements

- Full NETCONF protocol support using libnetconf2
- Configuration validation with YANG models
- Transaction support with rollback
- Support for additional interface types (bridge, VLAN)
- Firewall rule management
- Network namespace support
- Performance optimizations

## License

This project is provided as-is for educational and development purposes. 