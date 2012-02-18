// RUN: %compile %s -o %t && %run --trace --syms --symsub 0xC0000000 %t 2>&1 | %FileCheck %s
// XFAIL: Hosted
// XFAIL: X64

#if !defined(X86)
# error This test must be run on an x86 bare kernel!
#endif

// CHECK: higherhalf:
// CHECK: bringup:
// CHECK: earlyalloc:
// CHECK: tokenize:
// CHECK: main:
// CHECK: hlt