#!/usr/bin/env tclsh

package provide Test 1.0

set SHINERS [list CRESCENT 1 \
                  HALF     2 \
                  FULL     4]

namespace eval Test {
  namespace export {[A-Z]*}

  proc Toaster { t } {
    ttttt
  }
}

proc Test::PublicProc { moon { shine "shone" } } {
  puts "Moon: ${moon}"
  _PrivateProc [expr { $shine * 100 } ]
}

proc Test::_PrivateProc { shine } {
  for { set x 0 } { $x < $shine } { incr x } {
    puts "[expr { $x + 1 }]: Shine! [LikeAStar $x]"
  }
}

proc LikeAStar { x } {
  if { $x % 2 == 0 } {
    return "Like the star that you are"
  } else {
    return "Like a star"
  }
}

foreach { moon shiner } $SHINERS {
  Test::PublicProc $moon $shiner
}

set C sea
lappend SHINERS {*}[list A B $C]
