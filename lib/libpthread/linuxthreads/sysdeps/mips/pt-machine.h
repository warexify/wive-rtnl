/* Machine-dependent pthreads configuration and inline functions.

   Copyright (C) 1996, 1997, 1998, 2000, 2002 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ralf Baechle <ralf@gnu.org>.
   Based on the Alpha version by Richard Henderson <rth@tamu.edu>.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#ifndef _PT_MACHINE_H
#define _PT_MACHINE_H   1

#include <features.h>

#ifndef PT_EI
# define PT_EI extern inline
#endif

/* Copyright (C) 2000, 2002 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Maciej W. Rozycki <macro@ds2.pg.gda.pl>, 2000.  */
PT_EI int
_test_and_set (int *p, int v) __THROW
{
  int r, t;

  __asm__ __volatile__
    ("/* Inline test and set */\n"
     "1:\n\t"
     ".set	push\n\t"
     ".set	mips2\n\t"
     "ll	%0,%3\n\t"
     "move	%1,%4\n\t"
     "beq	%0,%4,2f\n\t"
     "sc	%1,%2\n\t"
     ".set	pop\n\t"
     "beqz	%1,1b\n"
     "2:\n\t"
     "/* End test and set */"
     : "=&r" (r), "=&r" (t), "=m" (*p)
     : "m" (*p), "r" (v)
     : "memory");

  return r;
}



extern long int testandset (int *spinlock);
extern int __compare_and_swap (long int *p, long int oldval, long int newval);


/* Spinlock implementation; required.  */

PT_EI long int
testandset (int *spinlock)
{
  return _test_and_set (spinlock, 1);
}


/* Get some notion of the current stack.  Need not be exactly the top
   of the stack, just something somewhere in the current frame.  */
#define CURRENT_STACK_FRAME  stack_pointer
register char * stack_pointer __asm__ ("$29");


/* Compare-and-swap for semaphores. */

#define HAS_COMPARE_AND_SWAP
PT_EI int
__compare_and_swap (long int *p, long int oldval, long int newval)
{
  long int ret, temp;

  __asm__ __volatile__
    ("/* Inline compare & swap */\n"
     "1:\n\t"
     ".set	push\n\t"
     ".set	mips2\n\t"
     "ll	%1,%5\n\t"
     "move	%0,$0\n\t"
     "bne	%1,%3,2f\n\t"
     "move	%0,$0\n\t" /*[NDF] Failure case. */
     "move	%0,%4\n\t"
     "sc	%0,%2\n\t"
     ".set	pop\n\t"
     "beqz	%0,1b\n"
     "2:\n\t"
     "/* End compare & swap */"
     : "=&r" (ret), "=&r" (temp), "=m" (*p)
     : "r" (oldval), "r" (newval), "m" (*p)
     : "memory");

  return ret;

  /*
    1:  load locked: into ret(%0), from *p(0(%4))
        branch to 2 if ret(%0) != oldval(%2)
         Delay slot: move 0 into ret(%0) // [NDF] Added
       Don't branch case:
       move newval(%3) into ret(%0)
       setcompare ret(%0) into *p(0(%1))
       branch to 1 if ret(%0) == 0 (sc failed)
         Delay slot: unknown/none
       return

    2: Delay slot
       return

ll a b
Sets a to the value pointed to by address b, and "locks" b so that if
any of a number of things are attempted that might access b then the
next sc will fail.

sc a b
Sets the memory address pointed to by b to the value in a atomically.
If it succeeds then a will be set to 1, if it fails a will be set to 0.

  */

}

#endif /* pt-machine.h */
