#!/bin/sh

set -eux

gcc -o test-netlink -Iinclude -I. $(pkg-config libnl-3.0 --cflags) test-netlink.c lldp_util.c lldp_rtnl.c $(pkg-config libnl-3.0 --libs) -Wall -Wextra -Werror -Wl,--wrap=ioctl
./test-netlink
