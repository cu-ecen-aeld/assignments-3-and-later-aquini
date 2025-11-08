# Debugging the faulty module
## Exercising the "faulty" module to force a system panic

By writing to the character device created after loading
the fault module, the system will trip on an exception 
and reboot after dumping the following trace to the console:
```
# echo “hello_world” > /dev/faulty 
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b67000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: faulty(O) hello(O) scull(O)
CPU: 0 PID: 154 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008ddbd20
x29: ffffffc008ddbd80 x28: ffffff8001b30000 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000000000012 x22: 0000000000000012 x21: ffffffc008ddbdc0
x20: 000000555cdba990 x19: ffffff8001bded00 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc00078c000 x3 : ffffffc008ddbdc0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```

## Breaking down the panic trace dump
The kernel does provide this trace dump dump in the hopes
of providing enough information to make it a little bit 
easier to start drilling down on the issue and start 
debugging it.

The trace dump carries information about the fault (Oops)
encountered by the kernel, in form of a textual message 
"Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000"
which goes followed by textual messages further describing the 
fault and the hardware state at the time of the fault. 
Different architectures might show slightly different messages 
at this section but, generally, all panic messages are similar to
the one observed above. Then the kernel provides information about
the loadable modules present in memory at the crash's time, anotating
their taint marks (all out-of-the-tree KO's, in this case), with
information about the system and the CPU/core that was running
code and tripped on the exception:
```
Modules linked in: faulty(O) hello(O) scull(O)
CPU: 0 PID: 154 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
```

Below that section, we get a dump of all CPU registers, the
stack trace (call trace) that led to the exception and the
opcodes that the CPU was exectuting prior to the crash.

The function that was executing at the time of the crash
is the symbol/address loaded at the CPU instruction pointer/
program counter: 
```
pc : faulty_write+0x10/0x20 [faulty]
```
and the kernel also annotates that the referred symbol is
part of a loadable module named faulty.

In order to decode the opcodes and see the instructions
that the CPU was executing leading to the crash we can
make use of Linux provided script "decodecode" as follows:
```
raquini@ubuntu:~/ECEA-5305/assignment-5-aquini$ awk '/Code:/ {print}' panic-trace | ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- buildroot/output/build/linux-6.1.44/scripts/decodecode 
Code: d2800001 d2800000 d503233f d50323bf (b900003f)
All code
========
   0:	d2800001 	mov	x1, #0x0                   	// #0
   4:	d2800000 	mov	x0, #0x0                   	// #0
   8:	d503233f 	paciasp
   c:	d50323bf 	autiasp
  10:*	b900003f 	str	wzr, [x1]		<-- trapping instruction

Code starting with the faulting instruction
===========================================
   0:	b900003f 	str	wzr, [x1]
```
This method doesn't provide us with a way to correlate Assembly instructions
and the module source code, but it is helpful nevertheless to confirm the
NULL pointer dereference bug in the code by looking into the trapping
instructions and the values stored in the register it uses as its operand. 
This technique can be utilized to enrich issue report to 3rd party vendors
when their modules cause a crash and we don't have the source code to proceed
on debugging.

## Debugging further
It becomes quite obvious what the problem is with faulty.ko as soon as we
take a look into its involved source code: 
```
 49 ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
 50                 loff_t *pos)
 51 {
 52         /* make a simple fault by dereferencing a NULL pointer */
 53         *(int *)0 = 0;
 54         return 0;
 55 }
```
Nevertheless, assuming this issue was not that obvious, we can get a similar sneak 
peek at the "faulty" function, just as the script "decodecode" gave us, 
by disassembling the compiled .KO as follows:
```
raquini@ubuntu:~/ECEA-5305/assignment-5-aquini$ aarch64-none-linux-gnu-objdump -Dl buildroot/output/build/ldd-55b3b5bd452068b7c125d204be5a3641e34c201b/misc-modules/faulty.o | grep -A15 "<faulty_write>:"
0000000000000000 <faulty_write>:
faulty_write():
   0:	d2800001 	mov	x1, #0x0                   	// #0
   4:	d2800000 	mov	x0, #0x0                   	// #0
   8:	d503233f 	paciasp
   c:	d50323bf 	autiasp
  10:	b900003f 	str	wzr, [x1]
  14:	d65f03c0 	ret
  18:	d503201f 	nop
  1c:	d503201f 	nop

0000000000000020 <faulty_init>:
faulty_init():
  20:	d503233f 	paciasp
  24:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
  28:	90000004 	adrp	x4, 0 <faulty_write>
```
We still don't have the matching source code lines, because the module was built without
debugging symbols. But we can, at this point, leverage BuildRoot build reproducibility
and adjust our module options to include debug information (-g) so the disassembly step
can provide us with more pointers on where to look next.

