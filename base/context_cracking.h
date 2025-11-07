#ifndef CONTEXT_CRACKING_H
#define CONTEXT_CRACKING_H

// Architecture Detection
#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_AMD64)
#define ARCH_X64 1
#elif defined(i386) || defined(__i386) || defined(__i386__) || defined(_M_IX86)
#define ARCH_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ARCH_ARM64 1
#elif defined(__arm__) || defined(_M_ARM)
#define ARCH_ARM32 1
#else
#error Architecture not supported.
#endif

#if ARCH_ARM32 || ARCH_ARM64 || ARCH_X64 || ARCH_X86
#define ARCH_LITTLE_ENDIAN 1
#else
#error Endianness of this architecture not understood.
#endif

// Build Option Cracking
#if !defined(BUILD_DEBUG)
#define BUILD_DEBUG 1
#endif

// Zero All Undefined Options
#if !defined(ARCH_X64)
#define ARCH_X64 0
#endif
#if !defined(ARCH_X86)
#define ARCH_X86 0
#endif
#if !defined(ARCH_ARM64)
#define ARCH_ARM64 0
#endif
#if !defined(ARCH_ARM32)
#define ARCH_ARM32 0
#endif

#endif // CONTEXT_CRACKING_H
