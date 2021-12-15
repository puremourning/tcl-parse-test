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

proc Test { a b c } {
  puts " Another one ? "
  ::Test
}

Test a b c

namespace eval Space {
  proc Race {} {
    ::Test a b c
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

proc ::Space2::Race2 { heaven } {
  Race
}
