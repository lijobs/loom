/*
 * Copyright (c) 1998, 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_RUNTIME_SYNCHRONIZER_HPP
#define SHARE_RUNTIME_SYNCHRONIZER_HPP

#include "memory/padded.hpp"
#include "oops/markWord.hpp"
#include "runtime/basicLock.hpp"
#include "runtime/handles.hpp"
#include "runtime/perfData.hpp"

class ObjectMonitor;
class ThreadsList;

struct DeflateMonitorCounters {
  int nInuse;              // currently associated with objects
  int nInCirculation;      // extant
  int nScavenged;          // reclaimed (global and per-thread)
  int perThreadScavenged;  // per-thread scavenge total
  double perThreadTimes;   // per-thread scavenge times
};

class ObjectSynchronizer : AllStatic {
  friend class VMStructs;
 public:
  typedef enum {
    owner_self,
    owner_none,
    owner_other
  } LockOwnership;

  typedef enum {
    inflate_cause_vm_internal = 0,
    inflate_cause_monitor_enter = 1,
    inflate_cause_wait = 2,
    inflate_cause_notify = 3,
    inflate_cause_hash_code = 4,
    inflate_cause_jni_enter = 5,
    inflate_cause_jni_exit = 6,
    inflate_cause_nof = 7 // Number of causes
  } InflateCause;

  // exit must be implemented non-blocking, since the compiler cannot easily handle
  // deoptimization at monitor exit. Hence, it does not take a Handle argument.

  // This is full version of monitor enter and exit. I choose not
  // to use enter() and exit() in order to make sure user be ware
  // of the performance and semantics difference. They are normally
  // used by ObjectLocker etc. The interpreter and compiler use
  // assembly copies of these routines. Please keep them synchronized.
  //
  // attempt_rebias flag is used by UseBiasedLocking implementation
  static void fast_enter(Handle obj, BasicLock* lock, bool attempt_rebias,
                         TRAPS);
  static void fast_exit(oop obj, BasicLock* lock, Thread* THREAD);

  // WARNING: They are ONLY used to handle the slow cases. They should
  // only be used when the fast cases failed. Use of these functions
  // without previous fast case check may cause fatal error.
  static void slow_enter(Handle obj, BasicLock* lock, TRAPS);
  static void slow_exit(oop obj, BasicLock* lock, Thread* THREAD);

  // Used only to handle jni locks or other unmatched monitor enter/exit
  // Internally they will use heavy weight monitor.
  static void jni_enter(Handle obj, TRAPS);
  static void jni_exit(oop obj, Thread* THREAD);

  // Handle all interpreter, compiler and jni cases
  static int  wait(Handle obj, jlong millis, TRAPS);
  static void notify(Handle obj, TRAPS);
  static void notifyall(Handle obj, TRAPS);

  static bool quick_notify(oopDesc* obj, Thread* Self, bool All);
  static bool quick_enter(oop obj, Thread* Self, BasicLock* Lock);

  // Special internal-use-only method for use by JVM infrastructure
  // that needs to wait() on a java-level object but that can't risk
  // throwing unexpected InterruptedExecutionExceptions.
  static void waitUninterruptibly(Handle obj, jlong Millis, Thread * THREAD);

  // used by classloading to free classloader object lock,
  // wait on an internal lock, and reclaim original lock
  // with original recursion count
  static intptr_t complete_exit(Handle obj, TRAPS);
  static void reenter (Handle obj, intptr_t recursion, TRAPS);

  // thread-specific and global objectMonitor free list accessors
  static ObjectMonitor * omAlloc(Thread * Self);
  static void omRelease(Thread * Self, ObjectMonitor * m,
                        bool FromPerThreadAlloc);
  static void omFlush(Thread * Self);

  // Inflate light weight monitor to heavy weight monitor
  static ObjectMonitor* inflate(Thread * Self, oop obj, const InflateCause cause);
  // This version is only for internal use
  static void inflate_helper(oop obj);
  static const char* inflate_cause_name(const InflateCause cause);

  // Returns the identity hash value for an oop
  // NOTE: It may cause monitor inflation
  static intptr_t identity_hash_value_for(Handle obj);
  static intptr_t FastHashCode(Thread * Self, oop obj);

  // java.lang.Thread support
  static bool current_thread_holds_lock(JavaThread* thread, Handle h_obj);
  static LockOwnership query_lock_ownership(JavaThread * self, Handle h_obj);

  static JavaThread* get_lock_owner(ThreadsList * t_list, Handle h_obj);

  // JNI detach support
  static void release_monitors_owned_by_thread(TRAPS);
  static void monitors_iterate(MonitorClosure* m);

  // GC: we current use aggressive monitor deflation policy
  // Basically we deflate all monitors that are not busy.
  // An adaptive profile-based deflation policy could be used if needed
  static void deflate_idle_monitors(DeflateMonitorCounters* counters);
  static void deflate_thread_local_monitors(Thread* thread, DeflateMonitorCounters* counters);
  static void prepare_deflate_idle_monitors(DeflateMonitorCounters* counters);
  static void finish_deflate_idle_monitors(DeflateMonitorCounters* counters);

  // For a given monitor list: global or per-thread, deflate idle monitors
  static int deflate_monitor_list(ObjectMonitor** listheadp,
                                  ObjectMonitor** freeHeadp,
                                  ObjectMonitor** freeTailp);
  static bool deflate_monitor(ObjectMonitor* mid, oop obj,
                              ObjectMonitor** freeHeadp,
                              ObjectMonitor** freeTailp);
  static bool is_cleanup_needed();
  static void oops_do(OopClosure* f);
  // Process oops in thread local used monitors
  static void thread_local_used_oops_do(Thread* thread, OopClosure* f);

  // debugging
  static void audit_and_print_stats(bool on_exit);
  static void chk_free_entry(JavaThread * jt, ObjectMonitor * n,
                             outputStream * out, int *error_cnt_p);
  static void chk_global_free_list_and_count(outputStream * out,
                                             int *error_cnt_p);
  static void chk_global_in_use_list_and_count(outputStream * out,
                                               int *error_cnt_p);
  static void chk_in_use_entry(JavaThread * jt, ObjectMonitor * n,
                               outputStream * out, int *error_cnt_p);
  static void chk_per_thread_in_use_list_and_count(JavaThread *jt,
                                                   outputStream * out,
                                                   int *error_cnt_p);
  static void chk_per_thread_free_list_and_count(JavaThread *jt,
                                                 outputStream * out,
                                                 int *error_cnt_p);
  static void log_in_use_monitor_details(outputStream * out, bool on_exit);
  static int  log_monitor_list_counts(outputStream * out);
  static int  verify_objmon_isinpool(ObjectMonitor *addr) PRODUCT_RETURN0;

 private:
  friend class SynchronizerTest;

  enum { _BLOCKSIZE = 128 };
  // global list of blocks of monitors
  static PaddedEnd<ObjectMonitor> * volatile gBlockList;
  // global monitor free list
  static ObjectMonitor * volatile gFreeList;
  // global monitor in-use list, for moribund threads,
  // monitors they inflated need to be scanned for deflation
  static ObjectMonitor * volatile gOmInUseList;
  // count of entries in gOmInUseList
  static int gOmInUseCount;

  // Process oops in all global used monitors (i.e. moribund thread's monitors)
  static void global_used_oops_do(OopClosure* f);
  // Process oops in monitors on the given list
  static void list_oops_do(ObjectMonitor* list, OopClosure* f);

  // Support for SynchronizerTest access to GVars fields:
  static u_char* get_gvars_addr();
  static u_char* get_gvars_hcSequence_addr();
  static size_t get_gvars_size();
  static u_char* get_gvars_stwRandom_addr();
};

// ObjectLocker enforces balanced locking and can never throw an
// IllegalMonitorStateException. However, a pending exception may
// have to pass through, and we must also be able to deal with
// asynchronous exceptions. The caller is responsible for checking
// the thread's pending exception if needed.
class ObjectLocker : public StackObj {
 private:
  Thread*   _thread;
  Handle    _obj;
  BasicLock _lock;
  bool      _dolock;   // default true
 public:
  ObjectLocker(Handle obj, Thread* thread, bool doLock = true);
  ~ObjectLocker();

  // Monitor behavior
  void wait(TRAPS)  { ObjectSynchronizer::wait(_obj, 0, CHECK); } // wait forever
  void notify_all(TRAPS)  { ObjectSynchronizer::notifyall(_obj, CHECK); }
  void waitUninterruptibly(TRAPS) { ObjectSynchronizer::waitUninterruptibly(_obj, 0, CHECK); }
  // complete_exit gives up lock completely, returning recursion count
  // reenter reclaims lock with original recursion count
  intptr_t complete_exit(TRAPS)  { return ObjectSynchronizer::complete_exit(_obj, THREAD); }
  void reenter(intptr_t recursion, TRAPS)  { ObjectSynchronizer::reenter(_obj, recursion, CHECK); }
};

#endif // SHARE_RUNTIME_SYNCHRONIZER_HPP
