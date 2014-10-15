/* Copyright (C) 2014 Free Software Foundation, Inc.
   Contributed by Iain Sandoe <iain@codesourcery.com>.

   This file is part of the GNU Atomic Library (libatomic).

   Libatomic is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   Libatomic is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

#include <stdint.h>
#include "libatomic_i.h"

#include <mach/mach_traps.h>
#include <mach/thread_switch.h>

/* For items that must be guarded by a lock, we use the following strategy:
   If atomic support is available for a unit32_t we use that.
   If not, we use the Darwin OSSpinLock implementation. */

/* Algorithm motivations.

Layout Assumptions:
  o Darwin has a number of sub-targets with common atomic types that have no
    'native' in-line handling, but are smaller than a cache-line.
    E.G. PPC32 needs locking for >= 8byte quantities, X86/m32 for >=16.

  o The _Atomic alignment of a "natural type" is no greater than the type size.

  o There are no special guarantees about the alignment of _Atomic aggregates
    other than those determined by the psABI.

  o There are no guarantees that placement of an entity won't cause it to
    straddle a cache-line boundary.

  o Realistic User code will likely place several _Atomic qualified types in
    close proximity (such that they fall within the same cache-line).
    Similarly, arrays of _Atomic qualified items.

Performance Assumptions:
  o Collisions of address hashes for items (which make up the lock keys)
    constitute the largest performance issue.

  o We want to avoid unnecessary flushing of [lock table] cache-line(s) when
    items are accessed.

Implementation:
  We maintain a table of locks, each lock being 4 bytes (at present).
  This choice of lock size gives some measure of equality in hash-collision
  statistics between the 'atomic' and 'OSSpinLock' implementations, since the
  lock size is fixed at 4 bytes for the latter.

  The table occupies one physical page, and we attempt to align it to a
  page boundary, appropriately.

  For entities that need a lock, with sizes < one cache line:
    Each entity that requires a lock, chooses the lock to use from the table on
  the basis of a hash determined by its size and address.  The lower log2(size)
  address bits are discarded on the assumption that the alignment of entities
  will not be smaller than their size.
  CHECKME: this is not verified for aggregates; it might be something that
  could/should be enforced from the front ends (since _Atomic types are
  allowed to have increased alignment c.f. 'normal').

  For entities that need a lock, with sizes >= one cacheline_size:
    We assume that the entity alignment >= log2(cacheline_size) and discard
  log2(cacheline_size) bits from the address.
  We then apply size/cacheline_size locks to cover the entity.

  The idea is that this will typically result in distinct hash keys for items
  placed close together.  The keys are mangled further such that the size is
  included in the hash.

  Finally, to attempt to make it such that the lock table entries are accessed
  in a scattered manner,to avoid repeated cacheline flushes, the hash is
  rearranged to attempt to maximise the most noise in the upper bits.
*/

/* The target page size.  Must be no larger than the runtime page size,
   lest locking fail with virtual address aliasing (i.e. a page mmaped
   at two locations).  */
#ifndef PAGE_SIZE
#  define PAGE_SIZE 4096
#endif

/* The target cacheline size.  */
#ifndef CACHLINE_SIZE
#  define CACHLINE_SIZE 64
#endif

/* The granularity at which locks are applied when n > CACHLINE_SIZE.
   We follow the posix pthreads implementation here.  */
#ifndef WATCH_SIZE
#  define WATCH_SIZE CACHLINE_SIZE
#endif

/* This is a number the number of tries we will make to acquire the lock
   before giving up our time-slice (on the basis that we are guarding small
   sections of code here and, therefore if we don't acquire the lock quickly,
   that implies that the current holder is not active).  */
#define NSPINS 4

#if HAVE_ATOMIC_EXCHANGE_4 && HAVE_ATOMIC_LDST_4

#  define LOCK_TYPE volatile uint32_t

/* So that we work with gcc-4.8 we don't try to use _Atomic.
   If _Atomic(uint32_t) ever gets greater alignment than 4 we'll need to
   revise this.  */

inline static void LockUnlock(LOCK_TYPE *l)
{
  __atomic_store_4((LOCK_TYPE *)l, 0, __ATOMIC_RELEASE);
}

inline static void LockLock(LOCK_TYPE *l)
{
  uint32_t old = 0;
  unsigned n = NSPINS;
  while (!__atomic_compare_exchange_4((LOCK_TYPE *)l, &old,
         1, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
      old = 0;
      if (--n == 0)
	{
	  /* Give up this time-slice, no hint to the scheduler about what to
	     pick.
	     TODO: maybe see if it's worth preserving some info about presence
	     of waiting processes - to allow a similar "give up" time-slice
	     scheme on the unlock end.  */
	  thread_switch((mach_port_name_t)0, SWITCH_OPTION_NONE,
			MACH_MSG_TIMEOUT_NONE);
	  n = NSPINS;
	}
    }
}

#else

#  include <libkern/OSAtomic.h>
#  define LOCK_TYPE OSSpinLock

#define LockUnlock(L) OSSpinLockUnlock(L)

inline static void LockLock(LOCK_TYPE *l)
{
  while (!OSSpinLockTry(l))
    {
      unsigned n = NSPINS;
      if (--n == 0)
	{
	  /* Give up this time-slice, no hint to the scheduler about what to
	     pick. (see comment above) */
	  thread_switch((mach_port_name_t)0, SWITCH_OPTION_NONE,
			MACH_MSG_TIMEOUT_NONE);
	  n = NSPINS;
	}
    }
}

#endif

#define LOCK_SIZE sizeof(LOCK_TYPE)
#define NLOCKS (PAGE_SIZE / LOCK_SIZE)
/* An array of locks, that should fill one physical page.  */
static LOCK_TYPE locks[NLOCKS] __attribute__((aligned(PAGE_SIZE)));

/* A hash function that assumes that entities of a given size are at least
   aligned to that size, and tries to minimise the probability that adjacent
   objects will end up using the same cache line in the locks.  */
static inline uintptr_t
addr_hash (void *ptr, size_t n)
{
  if (n <= CACHLINE_SIZE && n > 0)
    n = sizeof(unsigned int)*8 - __builtin_clz((unsigned int) n) -1;
  else
    n = 7;

  uint16_t x = (((uintptr_t)ptr) >> n);
  x ^= n;
  x = ((x >> 8) & 0xff) | ((x << 8) & 0xff00);
  return x % NLOCKS;
}

void
libat_lock_1 (void *ptr)
{
  LockLock (&locks[addr_hash (ptr, 1)]);
}

void
libat_unlock_1 (void *ptr)
{
  LockUnlock (&locks[addr_hash (ptr, 1)]);
}

void
libat_lock_n (void *ptr, size_t n)
{
  uintptr_t h = addr_hash (ptr, n);

  /* Don't lock more than all the locks we have.  */
  if (n > PAGE_SIZE)
    n = PAGE_SIZE;

  size_t i = 0;
  do
    {
      LockLock (&locks[h]);
      if (++h == NLOCKS)
	h = 0;
      i += WATCH_SIZE;
    }
  while (i < n);
}

void
libat_unlock_n (void *ptr, size_t n)
{
  uintptr_t h = addr_hash (ptr, n);

  if (n > PAGE_SIZE)
    n = PAGE_SIZE;

  size_t i = 0;
  do
    {
      LockUnlock (&locks[h]);
      if (++h == NLOCKS)
	h = 0;
      i += WATCH_SIZE;
    }
  while (i < n);
}
