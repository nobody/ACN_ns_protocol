set RATE 0.1
set ns [new Simulator]

#$ns rtproto DV

set nf [open tmp.nam w]
$ns namtrace-all $nf

set tr [open tmp.tr w]
$ns trace-all $tr


proc finish {} {
	global ns nf tr
	$ns flush-trace
	close $nf
	close $tr

	exec nam tmp.nam &
	exit 0
}


#set n0core [$ns node]
#set n0if0 [$ns node]
#set n0if1 [$ns node]
#
#set n1core [$ns node]
#set n1if0 [$ns node]
#set n1if1 [$ns node]
#
#$ns multihome-add-interface $n0core $n0if0
#$ns multihome-add-interface $n0core $n0if1
#
#$ns multihome-add-interface $n1core $n1if0
#$ns multihome-add-interface $n1core $n1if1
#
#$ns duplex-link $n0if0 $n1if0 .5Mb 200ms DropTail
#$ns duplex-link $n0if1 $n1if1 .5Mb 200ms DropTail
#
#set xyzzy0 [new Agent/Xyzzy]
#$ns multihome-attach-agent $n0core $xyzzy0
#
#set testData [new Application/testData]
#$testData attach-agent $xyzzy0
#
#set xyzzy1 [new Agent/Xyzzy]
#$ns multihome-attach-agent $n1core $xyzzy1
#
##$ns connect $tcp0 $tcpsink
##$ns connect $xyzzy0 $xyzzy1
#
#$xyzzy0 set class_ 1
#$ns color 1 Blue
#
#
#$ns at 0.5 "$testData start"
#$ns at 10.5 "$testData stop"
#
#$ns at 15.0 "finish"
#
#$ns run
#


set host0_core [$ns node]
set host0_if0 [$ns node]
set host0_if1 [$ns node]
$host0_core color Red
$host0_if0 color Red
$host0_if1 color Red
$ns multihome-add-interface $host0_core $host0_if0
$ns multihome-add-interface $host0_core $host0_if1

set host1_core [$ns node]
set host1_if0 [$ns node]
set host1_if1 [$ns node]
$host1_core color Blue
$host1_if0 color Blue
$host1_if1 color Blue
$ns multihome-add-interface $host1_core $host1_if0
$ns multihome-add-interface $host1_core $host1_if1

$ns duplex-link $host0_if0 $host1_if0 .5Mb 200ms DropTail
$ns duplex-link $host0_if1 $host1_if1 .5Mb 200ms DropTail

set xyzzy0 [new Agent/Xyzzy]
$ns multihome-attach-agent $host0_core $xyzzy0
#$xyzzy0 set fid_ 0
#$xyzzy0 set debugMask_ -1
#$xyzzy0 set debugFileIndex_ 0
#$xyzzy0 set mtu_ 1500
#$xyzzy0 set dataChunkSize_ 1468
#$xyzzy0 set numOutStreams_ 1
#$xyzzy0 set oneHeartbeatTimer_ 1   # one heartbeat timer shared for all dests

set trace_ch [open trace.xyzzy w]
$xyzzy0 set trace_all_ 1           # trace them all on oneline
$xyzzy0 trace cwnd_
$xyzzy0 trace rto_
$xyzzy0 trace errorCount_
$xyzzy0 attach $trace_ch

set xyzzy1 [new Agent/Xyzzy]
$ns multihome-attach-agent $host1_core $xyzzy1
#$xyzzy1 set debugMask_ -1
#$xyzzy1 set debugFileIndex_ 1
#$xyzzy1 set mtu_ 1500
#$xyzzy1 set initialRwnd_ 131072
#$xyzzy1 set useDelayedSacks_ 1

$ns color 0 Red
$ns color 1 Blue

$ns connect $xyzzy0 $xyzzy1

set ftp0 [new Application/testData]
$ftp0 attach-agent $xyzzy0

# set primary before association starts
$xyzzy0 set-primary-destination $host1_if0

# do a change primary in the middle of the association
#$ns at 7.5 "$xyzzy0 set-primary-destination $host1_if1"
#$ns at 7.5 "$xyzzy0 print cwnd_"

# simulate link failure
$ns rtmodel-at 5.0 down $host0_if0
$ns at 0.5 "$ftp0 start"
$ns at 10.0 "finish"

$ns run

