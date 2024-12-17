set -eu

yum install iproute-tc -y;
tc qdisc add dev eth0 root handle 1: tbf rate 100mbit burst 128kb latency 10ms;
tc qdisc add dev eth0 parent 1:1 handle 10: netem delay 10msec limit 8000

