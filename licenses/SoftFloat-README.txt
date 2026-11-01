MAME note: this package is derived from the following original SoftFloat 
package and has been "re-packaged" to work with MAME's conventions and
build system.  The source files come from bits64/ and bits64/templates
in the original distribution as MAME requires a compiler with a 64-bit
integer type.


Package Overview for SoftFloat Release 2b

John R. Hauser
2002 May 27


----------------------------------------------------------------------------
Overview

SoftFloat is a software implementation of floating-point that conforms to
the IEC/IEEE Standard for Binary Floating-Point Arithmetic.  SoftFloat is
distributed in the form of C source code.  Compiling the SoftFloat sources
generates two things:

-- A SoftFloat object file (typically `softfloat.o') containing the complete
   set of IEC/IEEE floating-point routines.

-- A `timesoftfloat' program for evaluating the speed of the SoftFloat
   routines.  (The SoftFloat module is linked into this program.)

The SoftFloat package is documented in four text files:

   SoftFloat.txt          Documentation for using the SoftFloat functions.
   SoftFloat-source.txt   Documentation for compiling SoftFloat.
   SoftFloat-history.txt  History of major changes to SoftFloat.
   timesoftfloat.txt      Documentation for using `timesoftfloat'.

Other files in the package comprise the source code for SoftFloat.

Please be aware that some work is involved in porting this software to other
targets.  It is not just a matter of getting `make' to complete without
error messages.  I would have written the code that way if I could, but
there are fundamental differences between systems that can't be hidden.
