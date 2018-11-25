# transocks-wong

## Feature

- Event-driven non-blocking I/O model
- IPv4 and IPv6 support, IPv6 works in dual stack mode
- SOCKS5 works in noauth mode
- Buffer copy via bufferevent provided by libevent
- Zero copy via splice() syscall provided by modern Linux kernel

## Prerequisite

netfilter_conntrack, iptables NAT/REDIRECT, modern Linux kernel with IPv6 support

## Usage

Run `transocks-wong -h` to check help text. You can send `SIGHUP` to dump all connection we are handling,
and send `SIGUSR1` to close all connection manually, it's equivalent to restart the program.

As usual, send `SIGTERM` or `SIGINT` to terminate.

examples:

```
transocks-wong --listener-addr-port=[::]:8123 --socks5-addr-port=[::1]:1081 --pump-method=splicepump
transocks-wong --listener-addr-port=0.0.0.0:8123 --socks5-addr-port=127.0.0.1:1081
```

## Other Tips

DNSMasq can be used to add resolved ip address.
```
# /etc/dnsmasq.conf
ipset=/<domain>/setmefree,setmefree6
```

Use two IPset, one for IPv4, the other one for IPv6, to support both.
```
## /etc/firewall.user(OpenWrt)
# drop old one
ipset -! destroy setmefree
ipset -! destroy setmefree6
# new ipset syntax, create TCP ipset
ipset -! create setmefree hash:net family inet
ipset -! create setmefree6 hash:net family inet6
# example to add IP range
#telegram IPs
ipset -! add setmefree 91.108.56.0/23
ipset -! add setmefree 91.108.56.0/22
ipset -! add setmefree 91.108.4.0/22
ipset -! add setmefree 149.154.172.0/22
ipset -! add setmefree 149.154.168.0/22
ipset -! add setmefree 149.154.164.0/22
ipset -! add setmefree 109.239.140.0/24

ipset -! add setmefree6 2001:b28:f23f::/48
ipset -! add setmefree6 2001:b28:f23d::/48
ipset -! add setmefree6 2001:67c:4e8::/48
# TCP redirect to TCP transparent proxy listening port
iptables -t nat -I PREROUTING -p tcp -m set --match-set setmefree dst -j REDIRECT --to-port 8123
# requires ip6tables nat module
ip6tables -t nat -I PREROUTING -p tcp -m set --match-set setmefree6 dst -j REDIRECT --to-port 8123
```
