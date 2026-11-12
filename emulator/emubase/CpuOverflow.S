/* Signed 8/16-bit overflow checks. The public ABI returns 0 or 1 as int. */
.text

#if defined(__x86_64__)
/* Windows passes arguments in cx/dx; System V uses di/si. */
#ifdef _WIN32
#define ARG8_A %cl
#define ARG8_B %dl
#define ARG16_A %cx
#define ARG16_B %dx
#else
#define ARG8_A %dil
#define ARG8_B %sil
#define ARG16_A %di
#define ARG16_B %si
#endif

.macro overflow name, operation, lhs, rhs
#ifdef __APPLE__
.globl _\name
_\name:
#else
.globl \name
\name:
#endif
    \operation \rhs, \lhs
    seto %al
    movzbl %al, %eax
    ret
.endm

overflow CpuAddOverflow8, addb, ARG8_A, ARG8_B
overflow CpuAddOverflow16, addw, ARG16_A, ARG16_B
overflow CpuSubOverflow8, subb, ARG8_A, ARG8_B
overflow CpuSubOverflow16, subw, ARG16_A, ARG16_B

#elif defined(__aarch64__) || defined(_M_ARM64)
/* Shift the signed operand's sign bit to bit 31, then use the V flag. */
.macro overflow name, operation, shift
#ifdef __APPLE__
.globl _\name
_\name:
#else
.globl \name
\name:
#endif
    lsl w0, w0, #\shift
    lsl w1, w1, #\shift
    \operation w0, w0, w1
    cset w0, vs
    ret
.endm

overflow CpuAddOverflow8, adds, 24
overflow CpuAddOverflow16, adds, 16
overflow CpuSubOverflow8, subs, 24
overflow CpuSubOverflow16, subs, 16
#else
#error Unsupported processor architecture
#endif

#if defined(__linux__)
.section .note.GNU-stack,"",@progbits
#endif
