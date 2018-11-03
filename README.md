# transocks-wong

## Precondition

netfilter_conntrack, iptables NAT/REDIRECT, modern Linux kernel with IPv6 support

## Usage

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
