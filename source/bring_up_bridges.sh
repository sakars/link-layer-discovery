#!/bin/bash

ip link add name br0 type bridge ; ip link set br0 up; ip addr add 192.168.10.1/24 dev br0

ip link add name br1 type bridge ; ip link set br1 up; ip addr add 192.168.20.1/24 dev br1

