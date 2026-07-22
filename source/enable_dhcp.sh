#!/bin/bash

# Toggle DHCP modes for br0: none, ipv4, ipv6, both

MODE="${1:-both}"

case "$MODE" in
  ipv4)
    echo "Enabling IPv4 DHCP only"
    dnsmasq --no-daemon --interface=br0 --bind-interfaces \
            --dhcp-range=192.168.10.100,192.168.10.200,255.255.255.0,1m
    ;;
  ipv6)
    echo "Enabling IPv6 DHCP only"
    dnsmasq --no-daemon --interface=br0 --bind-interfaces \
            --enable-ra \
            --dhcp-range=::100,::200,64,1m
    ;;
  both)
    echo "Enabling IPv4 and IPv6 DHCP"
    dnsmasq --no-daemon --interface=br0 --bind-interfaces \
            --dhcp-range=192.168.10.100,192.168.10.200,255.255.255.0,1m \
            --enable-ra \
            --dhcp-range=::100,::200,64,1m
    ;;
  *)
    echo "Unknown mode: $MODE"
    echo "Usage: $0 {none|ipv4|ipv6|both}"
    exit 1
    ;;
esac
