/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file
 * @brief IA-32 specific nanokernel interface header
 * This header contains the IA-32 specific nanokernel interface.  It is included
 * by the generic nanokernel interface header (nanokernel.h)
 */

#ifndef _ARCH_IFACE_H
#define _ARCH_IFACE_H

#include <irq.h>
#include <arch/x86/irq_controller.h>

#ifndef _ASMLANGUAGE
#include <arch/x86/asm_inline.h>
#include <arch/x86/addr_types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* APIs need to support non-byte addressable architectures */

#define OCTET_TO_SIZEOFUNIT(X) (X)
#define SIZEOFUNIT_TO_OCTET(X) (X)

/**
 * Macro used internally by NANO_CPU_INT_REGISTER and NANO_CPU_INT_REGISTER_ASM.
 * Not meant to be used explicitly by platform, driver or application code.
 */
#define MK_ISR_NAME(x) __isr__##x

#ifndef _ASMLANGUAGE

/* interrupt/exception/error related definitions */

/**
 * Floating point register set alignment.
 *
 * If support for SSEx extensions is enabled a 16 byte boundary is required,
 * since the 'fxsave' and 'fxrstor' instructions require this. In all other
 * cases a 4 byte boundary is sufficient.
 */

#ifdef CONFIG_SSE
#define FP_REG_SET_ALIGN  16
#else
#define FP_REG_SET_ALIGN  4
#endif

/*
 * The TCS must be aligned to the same boundary as that used by the floating
 * point register set.  This applies even for threads that don't initially
 * use floating point, since it is possible to enable floating point support
 * later on.
 */

#define STACK_ALIGN  FP_REG_SET_ALIGN

typedef struct s_isrList {
	/** Address of ISR/stub */
	void		*fnc;
	/** IRQ associated with the ISR/stub, or -1 if this is not
	 * associated with a real interrupt; in this case vec must
	 * not be -1
	 */
	unsigned int    irq;
	/** Priority associated with the IRQ. Ignored if vec is not -1 */
	unsigned int    priority;
	/** Vector number associated with ISR/stub, or -1 to assign based
	 * on priority
	 */
	unsigned int    vec;
	/** Privilege level associated with ISR/stub */
	unsigned int    dpl;
} ISR_LIST;


/**
 * @brief Connect a routine to an interrupt vector
 *
 * This macro "connects" the specified routine, @a r, to the specified interrupt
 * vector, @a v using the descriptor privilege level @a d. On the IA-32
 * architecture, an interrupt vector is a value from 0 to 255. This macro
 * populates the special intList section with the address of the routine, the
 * vector number and the descriptor privilege level. The genIdt tool then picks
 * up this information and generates an actual IDT entry with this information
 * properly encoded.
 *
 * The @a d argument specifies the privilege level for the interrupt-gate
 * descriptor; (hardware) interrupts and exceptions should specify a level of 0,
 * whereas handlers for user-mode software generated interrupts should specify 3.
 * @param r Routine to be connected
 * @param n IRQ number
 * @param p IRQ priority
 * @param v Interrupt Vector
 * @param d Descriptor Privilege Level
 *
 * @return N/A
 *
 */

#define NANO_CPU_INT_REGISTER(r, n, p, v, d) \
	 ISR_LIST __attribute__((section(".intList"))) MK_ISR_NAME(r) = \
			{&r, n, p, v, d}


/**
 * Code snippets for populating the vector ID and priority into the intList
 *
 * The 'magic' of static interrupts is accomplished by building up an array
 * 'intList' at compile time, and the gen_idt tool uses this to create the
 * actual IDT data structure.
 *
 * For controllers like APIC, the vectors in the IDT are not normally assigned
 * at build time; instead the sentinel value -1 is saved, and gen_idt figures
 * out the right vector to use based on our priority scheme. Groups of 16
 * vectors starting at 32 correspond to each priority level.
 *
 * On MVIC, the mapping is fixed; the vector to use is just the irq line
 * number plus 0x20. The priority argument supplied by the user is discarded.
 *
 * These macros are only intended to be used by IRQ_CONNECT() macro.
 */
#if CONFIG_X86_FIXED_IRQ_MAPPING
#define _VECTOR_ARG(irq_p)	_IRQ_CONTROLLER_VECTOR_MAPPING(irq_p)
#else
#define _VECTOR_ARG(irq_p)	(-1)
#endif /* CONFIG_X86_FIXED_IRQ_MAPPING */

/**
 * Configure a static interrupt.
 *
 * All arguments must be computable by the compiler at build time.
 *
 * Internally this function does a few things:
 *
 * 1. There is a declaration of the interrupt parameters in the .intList
 * section, used by gen_idt to create the IDT. This does the same thing
 * as the NANO_CPU_INT_REGISTER() macro, but is done in assembly as we
 * need to populate the .fnc member with the address of the assembly
 * IRQ stub that we generate immediately afterwards.
 *
 * 2. The IRQ stub itself is declared. The code will go in its own named
 * section .text.irqstubs section (which eventually gets linked into 'text')
 * and the stub shall be named (isr_name)_irq(irq_line)_stub
 *
 * 3. The IRQ stub pushes the ISR routine and its argument onto the stack
 * and then jumps to the common interrupt handling code in _interrupt_enter().
 *
 * 4. _irq_controller_irq_config() is called at runtime to set the mapping
 * between the vector and the IRQ line as well as triggering flags
 *
 * @param irq_p IRQ line number
 * @param priority_p Interrupt priority
 * @param isr_p Interrupt service routine
 * @param isr_param_p ISR parameter
 * @param flags_p IRQ triggering options, as defined in irq_controller.h
 *
 * @return The vector assigned to this interrupt
 */
#define _ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p) \
({ \
	__asm__ __volatile__(							\
		".pushsection .intList\n\t" \
		".long %P[isr]_irq%P[irq]_stub\n\t"	/* ISR_LIST.fnc */ \
		".long %P[irq]\n\t"		/* ISR_LIST.irq */ \
		".long %P[priority]\n\t"	/* ISR_LIST.priority */ \
		".long %P[vector]\n\t"		/* ISR_LIST.vec */ \
		".long 0\n\t"			/* ISR_LIST.dpl */ \
		".popsection\n\t" \
		".pushsection .text.irqstubs\n\t" \
		".global %P[isr]_irq%P[irq]_stub\n\t" \
		"%P[isr]_irq%P[irq]_stub:\n\t" \
		"pushl %[isr_param]\n\t" \
		"pushl %[isr]\n\t" \
		"jmp _interrupt_enter\n\t" \
		".popsection\n\t" \
		: \
		: [isr] "i" (isr_p), \
		  [isr_param] "i" (isr_param_p), \
		  [priority] "i" (priority_p), \
		  [vector] "i" _VECTOR_ARG(irq_p), \
		  [irq] "i" (irq_p)); \
	_irq_controller_irq_config(_IRQ_TO_INTERRUPT_VECTOR(irq_p), (irq_p), \
				   (flags_p)); \
	_IRQ_TO_INTERRUPT_VECTOR(irq_p); \
})

#ifdef CONFIG_X86_FIXED_IRQ_MAPPING
/* Fixed vector-to-irq association mapping.
 * No need for the table at all.
 */
#define _IRQ_TO_INTERRUPT_VECTOR(irq) _IRQ_CONTROLLER_VECTOR_MAPPING(irq)
#else
/**
 * @brief Convert a statically connected IRQ to its interrupt vector number
 *
 * @param irq IRQ number
 */
extern unsigned char _irq_to_interrupt_vector[];
#define _IRQ_TO_INTERRUPT_VECTOR(irq)                       \
			((unsigned int) _irq_to_interrupt_vector[irq])
#endif


/**
 * @brief Nanokernel Exception Stack Frame
 *
 * A pointer to an "exception stack frame" (ESF) is passed as an argument
 * to exception handlers registered via nanoCpuExcConnect().  As the system
 * always operates at ring 0, only the EIP, CS and EFLAGS registers are pushed
 * onto the stack when an exception occurs.
 *
 * The exception stack frame includes the volatile registers (EAX, ECX, and
 * EDX) as well as the 5 non-volatile registers (EDI, ESI, EBX, EBP and ESP).
 * Those registers are pushed onto the stack by _ExcEnt().
 */

typedef struct nanoEsf {
	unsigned int esp;
	unsigned int ebp;
	unsigned int ebx;
	unsigned int esi;
	unsigned int edi;
	unsigned int edx;
	unsigned int eax;
	unsigned int ecx;
	unsigned int errorCode;
	unsigned int eip;
	unsigned int cs;
	unsigned int eflags;
} NANO_ESF;

/**
 * @brief Nanokernel "interrupt stack frame" (ISF)
 *
 * An "interrupt stack frame" (ISF) as constructed by the processor and the
 * interrupt wrapper function _interrupt_enter().  As the system always
 * operates at ring 0, only the EIP, CS and EFLAGS registers are pushed onto
 * the stack when an interrupt occurs.
 *
 * The interrupt stack frame includes the volatile registers EAX, ECX, and EDX
 * plus nonvolatile EDI pushed on the stack by _interrupt_enter().
 *
 * Only target-based debug tools such as GDB require the other non-volatile
 * registers (ESI, EBX, EBP and ESP) to be preserved during an interrupt.
 */

typedef struct nanoIsf {
#ifdef CONFIG_DEBUG_INFO
	unsigned int esp;
	unsigned int ebp;
	unsigned int ebx;
	unsigned int esi;
#endif /* CONFIG_DEBUG_INFO */
	unsigned int edi;
	unsigned int ecx;
	unsigned int edx;
	unsigned int eax;
	unsigned int eip;
	unsigned int cs;
	unsigned int eflags;
} NANO_ISF;

#endif /* !_ASMLANGUAGE */

/*
 * Reason codes passed to both _NanoFatalErrorHandler()
 * and _SysFatalErrorHandler().
 */

/** Unhandled exception/interrupt */
#define _NANO_ERR_SPURIOUS_INT		 (0)
/** Page fault */
#define _NANO_ERR_PAGE_FAULT		 (1)
/** General protection fault */
#define _NANO_ERR_GEN_PROT_FAULT	 (2)
/** Invalid task exit */
#define _NANO_ERR_INVALID_TASK_EXIT  (3)
/** Stack corruption detected */
#define _NANO_ERR_STACK_CHK_FAIL	 (4)
/** Kernel Allocation Failure */
#define _NANO_ERR_ALLOCATION_FAIL    (5)
/** Unhandled exception */
#define _NANO_ERR_CPU_EXCEPTION		(6)

#ifndef _ASMLANGUAGE

#ifdef CONFIG_INT_LATENCY_BENCHMARK
void _int_latency_start(void);
void _int_latency_stop(void);
#else
#define _int_latency_start()  do { } while (0)
#define _int_latency_stop()   do { } while (0)
#endif

/**
 * @brief Disable all interrupts on the CPU (inline)
 *
 * This routine disables interrupts.  It can be called from either interrupt,
 * task or fiber level.  This routine returns an architecture-dependent
 * lock-out key representing the "interrupt disable state" prior to the call;
 * this key can be passed to irq_unlock() to re-enable interrupts.
 *
 * The lock-out key should only be used as the argument to the irq_unlock()
 * API.  It should never be used to manually re-enable interrupts or to inspect
 * or manipulate the contents of the source register.
 *
 * This function can be called recursively: it will return a key to return the
 * state of interrupt locking to the previous level.
 *
 * WARNINGS
 * Invoking a kernel routine with interrupts locked may result in
 * interrupts being re-enabled for an unspecified period of time.  If the
 * called routine blocks, interrupts will be re-enabled while another
 * thread executes, or while the system is idle.
 *
 * The "interrupt disable state" is an attribute of a thread.  Thus, if a
 * fiber or task disables interrupts and subsequently invokes a kernel
 * routine that causes the calling thread to block, the interrupt
 * disable state will be restored when the thread is later rescheduled
 * for execution.
 *
 * @return An architecture-dependent lock-out key representing the
 * "interrupt disable state" prior to the call.
 *
 */

static ALWAYS_INLINE unsigned int _arch_irq_lock(void)
{
	unsigned int key = _do_irq_lock();

	_int_latency_start();

	return key;
}


/**
 *
 * @brief Enable all interrupts on the CPU (inline)
 *
 * This routine re-enables interrupts on the CPU.  The @a key parameter
 * is an architecture-dependent lock-out key that is returned by a previous
 * invocation of irq_lock().
 *
 * This routine can be called from either interrupt, task or fiber level.
 *
 * @return N/A
 *
 */

static ALWAYS_INLINE void _arch_irq_unlock(unsigned int key)
{
	if (!(key & 0x200)) {
		return;
	}

	_int_latency_stop();

	_do_irq_unlock();
}

/**
 * The NANO_SOFT_IRQ macro must be used as the value for the @a irq parameter
 * to NANO_CPU_INT_REGSITER when connecting to an interrupt that does not
 * correspond to any IRQ line (such as spurious vector or SW IRQ)
 */
#define NANO_SOFT_IRQ	((unsigned int) (-1))

#ifdef CONFIG_FP_SHARING
/* Definitions for the 'options' parameter to the fiber_fiber_start() API */

/** thread uses floating point unit */
#define USE_FP		0x10
#ifdef CONFIG_SSE
/** thread uses SSEx instructions */
#define USE_SSE		0x20
#endif /* CONFIG_SSE */
#endif /* CONFIG_FP_SHARING */

/**
 * @brief Enable a specific IRQ
 * @param irq IRQ
 */
extern void	_arch_irq_enable(unsigned int irq);
/**
 * @brief Disable a specific IRQ
 * @param irq IRQ
 */
extern void	_arch_irq_disable(unsigned int irq);

#ifdef CONFIG_FP_SHARING
#ifdef CONFIG_KERNEL_V2
extern void k_float_enable(k_tid_t thread_id, unsigned int options);
extern void k_float_disable(k_tid_t thread_id);
#else
/**
 * @brief Enable floating point hardware resources sharing
 * Dynamically enable/disable the capability of a thread to share floating
 * point hardware resources.  The same "floating point" options accepted by
 * fiber_fiber_start() are accepted by these APIs (i.e. USE_FP and USE_SSE).
 */
extern void	fiber_float_enable(nano_thread_id_t thread_id,
								unsigned int options);
extern void	task_float_enable(nano_thread_id_t thread_id,
								unsigned int options);
extern void	fiber_float_disable(nano_thread_id_t thread_id);
extern void	task_float_disable(nano_thread_id_t thread_id);

#endif /* CONFIG_KERNEL_V2 */
#endif /* CONFIG_FP_SHARING */

#include <stddef.h>	/* for size_t */

extern void	nano_cpu_idle(void);

/** Nanokernel provided routine to report any detected fatal error. */
extern FUNC_NORETURN void _NanoFatalErrorHandler(unsigned int reason,
						 const NANO_ESF * pEsf);
/** User provided routine to handle any detected fatal error post reporting. */
extern FUNC_NORETURN void _SysFatalErrorHandler(unsigned int reason,
						const NANO_ESF * pEsf);
/** Dummy ESF for fatal errors that would otherwise not have an ESF */
extern const NANO_ESF _default_esf;

#endif /* !_ASMLANGUAGE */

/* reboot through Reset Control Register (I/O port 0xcf9) */

#define SYS_X86_RST_CNT_REG 0xcf9
#define SYS_X86_RST_CNT_SYS_RST 0x02
#define SYS_X86_RST_CNT_CPU_RST 0x4
#define SYS_X86_RST_CNT_FULL_RST 0x08

#ifdef __cplusplus
}
#endif

#endif /* _ARCH_IFACE_H */
