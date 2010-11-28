set RATE 0.1
set ns [new Simulator]

#$ns rtproto DV

set nf [open buddy.nam w]
$ns namtrace-all $nf

set tr [open buddy.tr w]
$ns trace-all $tr


proc finish {} {
	global ns nf tr
	$ns flush-trace
	close $nf
	close $tr

	#exec nam tmp.nam &
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
set host0_bif0 [$ns node]
set host0_bif1 [$ns node]
$host0_core color Red
$host0_core addr 0
$host0_if0 color Red
$host0_if0 addr 1
$host0_if1 color Red
$host0_if1 addr 2
$host0_bif0 color Red
$host0_bif0 addr 3
$host0_bif1 color Red
$host0_bif1 addr 4
$ns multihome-add-interface $host0_core $host0_if0
$ns multihome-add-interface $host0_core $host0_if1
$ns multihome-add-interface $host0_core $host0_bif0
$ns multihome-add-interface $host0_core $host0_bif1

set host1_core [$ns node]
set host1_if0 [$ns node]
set host1_if1 [$ns node]
set host1_if2 [$ns node]
$host1_core color Blue
$host1_core addr 5
$host1_if0 color Blue
$host1_if0 addr 6
$host1_if1 color Blue
$host1_if1 addr 7
$host1_if2 color Blue
$host1_if2 addr 8
$ns multihome-add-interface $host1_core $host1_if0
$ns multihome-add-interface $host1_core $host1_if1
$ns multihome-add-interface $host1_core $host1_if2

set host2_core [$ns node]
set host2_if0 [$ns node]
set host2_bif0 [$ns node]
set host2_bif1 [$ns node]
$host2_core color Green
$host2_core addr 9
$host2_if0 color Green
$host2_if0 addr 10
$host2_bif0 color Green
$host2_bif0 addr 11
$host2_bif1 color Green
$host2_bif1 addr 12
$ns multihome-add-interface $host2_core $host2_if0
$ns multihome-add-interface $host2_core $host2_bif0
$ns multihome-add-interface $host2_core $host2_bif1

$ns duplex-link $host0_if0 $host1_if0 5Mb 20ms DropTail
$ns duplex-link $host0_if1 $host1_if1 5Mb 20ms DropTail

$ns duplex-link $host2_if0 $host1_if2 5Mb 20ms DropTail

$ns duplex-link $host0_bif0 $host2_bif0 5Mb 20ms DropTail
$ns duplex-link $host0_bif1 $host2_bif1 5Mb 20ms DropTail

set xyzzy0 [new Agent/Xyzzy]
$ns multihome-attach-agent $host0_core $xyzzy0
$xyzzy0 set id_ 0

set xyzzy1 [new Agent/Xyzzy]
$ns multihome-attach-agent $host1_core $xyzzy1
$xyzzy0 set id_ 1

set xyzzy2 [new Agent/Xyzzy]
$ns multihome-attach-agent $host2_core $xyzzy2
$xyzzy0 set id_ 2

$ns color 0 Red
$ns color 1 Blue
$ns color 2 Green


$xyzzy0 add-multihome-destination 6 0
$xyzzy1 add-multihome-destination 1 0

$xyzzy0 add-multihome-destination 7 0
$xyzzy1 add-multihome-destination 2 0

$xyzzy2 add-multihome-destination 8 0
$xyzzy1 add-multihome-destination 10 0

$xyzzy0 add-buddy-destination $xyzzy2 11 0
$xyzzy2 add-buddy-destination $xyzzy0 3 0

$xyzzy0 add-buddy-destination $xyzzy2 12 0
$xyzzy2 add-buddy-destination $xyzzy0 4 0


set ftp0 [new Application/testData]
$ftp0 attach-agent $xyzzy0

# set primary before association starts
$xyzzy0 set-primary-destination $host1_if0

# do a change primary in the middle of the association
#$ns at 7.5 "$xyzzy0 set-primary-destination $host1_if1"
#$ns at 7.5 "$xyzzy0 print cwnd_"

# simulate link failure
$ns rtmodel-at 2.0 down $host0_if0
$ns rtmodel-at 7.0 up $host0_if0
$ns at 0.5 "$ftp0 start"
$ns at 10.0 "finish"

$ns run

