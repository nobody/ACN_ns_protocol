set RATE 0.1
set ns [new Simulator]

#$ns rtproto DV

set nf [open base_buddy.nam w]
$ns namtrace-all $nf

set tr [open base_buddy.tr w]
$ns trace-all $tr


proc finish {} {
	global ns nf tr
	$ns flush-trace
	close $nf
	close $tr

	#exec nam tmp.nam &
	exit 0
}

# send host
set host0_core [$ns node]
set host0_if0 [$ns node]
set host0_if1 [$ns node]
set host0_bif0 [$ns node]
$host0_core color Red
$host0_core addr 0
$host0_if0 color Red
$host0_if0 addr 1
$host0_if1 color Red
$host0_if1 addr 2
$host0_bif0 color Red
$host0_bif0 addr 3
$ns multihome-add-interface $host0_core $host0_if0
$ns multihome-add-interface $host0_core $host0_if1
$ns multihome-add-interface $host0_core $host0_bif0

# dest host
set host1_core [$ns node]
set host1_if0 [$ns node]
set host1_if1 [$ns node]
set host1_bif0 [$ns node]
$host1_core color Blue
$host1_core addr 4
$host1_if0 color Blue
$host1_if0 addr 5
$host1_if1 color Blue
$host1_if1 addr 6
$host1_bif0 color Blue
$host1_bif0 addr 7
$ns multihome-add-interface $host1_core $host1_if0
$ns multihome-add-interface $host1_core $host1_if1
$ns multihome-add-interface $host1_core $host1_bif0

# send buddy
set host2_core [$ns node]
set host2_if0 [$ns node]
set host2_if1 [$ns node]
set host2_bif0 [$ns node]
$host2_core color Green
$host2_core addr 8
$host2_if0 color Green
$host2_if0 addr 9
$host2_if1 color Green
$host2_if1 addr 10
$host2_bif0 color Green
$host2_bif0 addr 11
$ns multihome-add-interface $host2_core $host2_if0
$ns multihome-add-interface $host2_core $host2_if1
$ns multihome-add-interface $host2_core $host2_bif0

# dest buddy
set host3_core [$ns node]
set host3_if0 [$ns node]
set host3_if1 [$ns node]
set host3_bif0 [$ns node]
$host3_core color Yellow
$host3_core addr 12
$host3_if0 color Yellow
$host3_if0 addr 13
$host3_if1 color Yellow
$host3_if1 addr 14
$host3_bif0 color Yellow
$host3_bif0 addr 15
$ns multihome-add-interface $host3_core $host3_if0
$ns multihome-add-interface $host3_core $host3_if1
$ns multihome-add-interface $host3_core $host3_bif0

$ns duplex-link $host0_if0 $host1_if0 5Mb 20ms DropTail
$ns duplex-link $host0_if1 $host1_if1 5Mb 20ms DropTail

$ns duplex-link $host2_if0 $host3_if0 5Mb 20ms DropTail
$ns duplex-link $host2_if1 $host3_if1 5Mb 20ms DropTail

$ns duplex-link $host0_bif0 $host2_bif0 5Mb 20ms DropTail
$ns queue-limit $host0_bif0 $host2_bif0 1000
$ns queue-limit $host2_bif0 $host0_bif0 1000

$ns duplex-link $host1_bif0 $host3_bif0 5Mb 20ms DropTail
$ns queue-limit $host1_bif0 $host3_bif0 1000
$ns queue-limit $host3_bif0 $host1_bif0 1000

set xyzzy0 [new Agent/Xyzzy]
$ns multihome-attach-agent $host0_core $xyzzy0
$xyzzy0 set id_ 0
$xyzzy0 set isActiveBuddy 1

set xyzzy1 [new Agent/Xyzzy]
$ns multihome-attach-agent $host1_core $xyzzy1
$xyzzy1 set id_ 1
$xyzzy1 set isActiveBuddy 1

set xyzzy2 [new Agent/Xyzzy]
$ns multihome-attach-agent $host2_core $xyzzy2
$xyzzy2 set id_ 2

set xyzzy3 [new Agent/Xyzzy]
$ns multihome-attach-agent $host3_core $xyzzy3
$xyzzy3 set id_ 3

$ns color 0 Red
$ns color 1 Blue
$ns color 2 Green
$ns color 3 Yellow


$xyzzy0 add-multihome-destination 5 0
$xyzzy1 add-multihome-destination 1 0

$xyzzy0 add-multihome-destination 6 0
$xyzzy1 add-multihome-destination 2 0

$xyzzy2 add-multihome-destination 13 0
$xyzzy3 add-multihome-destination 9 0

$xyzzy2 add-multihome-destination 14 0
$xyzzy3 add-multihome-destination 10 0

$xyzzy0 add-buddy-destination $xyzzy2 11 0
$xyzzy2 add-buddy-destination $xyzzy0 3 0

$xyzzy1 add-buddy-destination $xyzzy3 15 0
$xyzzy3 add-buddy-destination $xyzzy1 7 0


set ftp0 [new Application/testFile $xyzzy0]
#$ftp0 attach-agent $xyzzy0

set ftp1 [new Application/testFile $xyzzy1]
#$ftp1 attach-agent $xyzzy1

set ftp2 [new Application/testFile $xyzzy2]


# set primary before association starts
$xyzzy0 set-primary-destination $host1_if0

# simulate node failure
set KILL_TIME 3.0
$ns rtmodel-at $KILL_TIME down $host0_if0
$ns rtmodel-at $KILL_TIME down $host0_if1
$ns rtmodel-at $KILL_TIME down $host0_bif0
$ns at $KILL_TIME "$ftp0 stop"

#set START_TIME 6.0
#$ns rtmodel-at $START_TIME up $host0_if0
#$ns rtmodel-at $START_TIME up $host0_if1
#$ns rtmodel-at $START_TIME up $host0_bif0

$ns at 0.5 "$ftp0 start"
$ns at 20.0 "finish"

$ns run

