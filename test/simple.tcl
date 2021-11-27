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
      Race [::Toast]
      Race "[Toast]"
    }
  }

}


namespace eval Space2 {
  proc Race {} {
    ::Test
  }

  namespace eval Rice {
    proc Roast {} {
      Race
    }
  }
}
