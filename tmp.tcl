set RATE 0.02
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

	#exec nam tmp.nam &
	#exec /net/core/export/home/cs/001/c/courses/cs6390002/acngr01/tp.pl tmp.tr
	exit 0
}


set n0 [$ns node]
set n1 [$ns node]

$ns duplex-link $n0 $n1 100Mb 5ms DropTail

$ns duplex-link-op $n0 $n1 orient right
$ns duplex-link-op $n0 $n1 queuePos 0.5

set secs [clock clicks -milliseconds]
set seed  [expr $secs % 2000000000]
set rng [new RNG]
$rng seed $seed

set rv [new RandomVariable/Uniform]
$rv use-rng $rng
set lossmodel [new ErrorModel]
$lossmodel unit pkt
$lossmodel set rate_ $RATE
$lossmodel ranvar $rv
$ns link-lossmodel $lossmodel $n0 $n1
#$lossmodel drop-target [new Agent/Null]

set tcp0 [new Agent/Xyzzy]
#$tcp0 set packetSize_ 200
#$tcp0 set window_ 60
$ns attach-agent $n0 $tcp0

set ftp [new Application/testData]
#$ftp set interval_ 0.00005
#$ftp set rate_ 20Mb
#$ftp set packetSize_ 200
$ftp attach-agent $tcp0

set tcpsink [new Agent/Xyzzy]
$ns attach-agent $n1 $tcpsink

$ns connect $tcp0 $tcpsink

$tcp0 set class_ 1

$ns color 1 Blue


$ns at 0.5 "$ftp start"
$ns at 10.5 "$ftp stop"

$ns at 15.0 "finish"

$ns run
