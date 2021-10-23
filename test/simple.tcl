#!/usr/bin/env tcl

proc Toast {} {
  puts "Toaster"
}

proc Test {} {
  puts "test"
  Toast

  puts [Toast]
}

Test

namespace eval Space {
  proc Race {} {
    ::Test
  }

  namespace eval Rice {
    proc Roast {} {
      Race
    }
  }

}

