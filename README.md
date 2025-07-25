# FreeBSD Network Configuration Tool

A FreeBSD application that provides ifconfig and route functionality using a NETCONF-style command syntax. The application consists of a client (`net`) and server (`netd`) that communicate via UNIX sockets.

## Features

- NETCONF-style command syntax
- Interactive and one-shot command modes
- Interface configuration
- Route management
- VRF (Virtual Routing and Forwarding) support
- Configuration persistence
- Standard IETF YANG model integration (RFC 8343, RFC 8344, RFC 8349, RFC 8529)

## Prerequisites

- FreeBSD system
- libnetconf2 development libraries
- libyang development libraries
- Standard IETF YANG models (included in yang/std/standard/ietf/RFC/)

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
```

## YANG Models

This project uses standard IETF YANG models with custom FreeBSD-specific augmentations:

### Standard IETF Models:
- **ietf-interfaces** (RFC 8343): Interface management and configuration
- **ietf-ip** (RFC 8344): IP address configuration for interfaces  
- **ietf-routing** (RFC 8349): Routing table management and static routes
- **ietf-network-instance** (RFC 8529): Virtual Routing and Forwarding (VRF) management
- **ietf-inet-types**: Common Internet address and port types
- **ietf-yang-types**: Common YANG data types
- **iana-if-type**: Standard interface type definitions

### Custom Augmentations:
- **netd-augments**: FreeBSD-specific extensions including:
  - `tunnel-vrf`: Tunnel VRF binding for advanced FreeBSD tunnel routing
  - Platform-specific features not covered by standard IETF models

The standard models are located in `yang/std/standard/ietf/RFC/` and custom 
augmentations in `yang/netd-augments.yang`. This approach provides standards-compliant 
NETCONF operations while supporting FreeBSD-specific functionality.

## License

Copyright (c) 2025 Paige Thompson / Raven Hammer Research (paige@paige.bio)

This project is licensed under the BSD 3-Clause License. See the [LICENSE](LICENSE) file for details.
