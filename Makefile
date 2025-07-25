CC=		cc
CFLAGS=		-Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -I/usr/local/include
LDFLAGS=	-L/usr/local/lib -lnetconf2 -lyang -lutil -lreadline -lexpat
# Build targets
all: net netd test_commands

net: client.o netconf_client.o commands.o table.o
	$(CC) -o net client.o netconf_client.o commands.o table.o $(LDFLAGS)

netd: server.o netconf_server.o netconf_yang.o ifconfig.o route.o commands.o
	$(CC) -o netd server.o netconf_server.o netconf_yang.o ifconfig.o route.o commands.o $(LDFLAGS)

test_commands: commands.o
	@mkdir -p tests/unit
	$(CC) $(CFLAGS) -o tests/unit/test_commands tests/unit/test_commands.c commands.o $(LDFLAGS)

# Object files
client.o: src/client.c src/common.h
	$(CC) $(CFLAGS) -c src/client.c

netconf_client.o: src/netconf_client.c src/common.h
	$(CC) $(CFLAGS) -c src/netconf_client.c

commands.o: src/commands.c src/common.h
	$(CC) $(CFLAGS) -c src/commands.c

table.o: src/table.c src/table.h src/common.h
	$(CC) $(CFLAGS) -c src/table.c

server.o: src/server.c src/common.h
	$(CC) $(CFLAGS) -c src/server.c

netconf_server.o: src/netconf_server.c src/common.h
	$(CC) $(CFLAGS) -c src/netconf_server.c

netconf_yang.o: src/netconf_yang.c src/common.h
	$(CC) $(CFLAGS) -c src/netconf_yang.c

ifconfig.o: src/ifconfig.c src/common.h
	$(CC) $(CFLAGS) -c src/ifconfig.c

route.o: src/route.c src/common.h
	$(CC) $(CFLAGS) -c src/route.c

clean:
	rm -f *.o net netd
	rm -rf tests/unit/test_commands

install: net netd
	install -m 755 net /usr/local/bin/
	install -m 755 netd /usr/local/sbin/
	# Install custom YANG augmentation for FreeBSD-specific features
	install -d /usr/local/share/yang
	install -m 644 yang/netd-augments.yang /usr/local/share/yang/
	# Note: Standard IETF YANG models loaded from yang/std/standard/ietf/RFC/

.PHONY: all clean install
