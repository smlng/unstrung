#!/bin/sh

TEST_PURPOSE=regress
TEST_PROB_REPORT=0
TEST_TYPE=pkttest

XHOST_LIST="E"
XNET_LIST="ground n1"

TESTNAME=recv-03
EHOST=E

N1_PLAY=../../unit/INPUTS/dio-02.pcap
REF_N1_OUTPUT=n1-output.txt
REF_N1_FILTER="no-icmp-v6-mcast.sed"
REF_E_CONSOLE_OUTPUT=econsole.txt
REF_CONSOLE_FIXUPS="script-only.sed "
REF_CONSOLE_FIXUPS="${REF_CONSOLE_FIXUPS} remove_dummy.sed"
E_RUN_SCRIPT=sunshine.sh
E_FINAL_SCRIPT=sunfinal.sh

PACKETRATE=100

