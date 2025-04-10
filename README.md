# LDB

LDB is a debugging tool for C++ programs.

cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake

## Attaching to a Process

### Process Interaction

#### ptrace

*inferior process* refer to the process which we want to debug.

```C++
long ptrace(enum __ptrace_request request, pid_t pid, void* addr, void* data);
```

For request parameter:

- **PTRACE_PEEKDATA**: Reads 8 bytes of memory at the given address.
- **PTRACE_ATTACH**: Attach to the existing process(pid).
- **PTRACE_GETREGS**: Retrieves the current values of all CPU registers.
- **PTRACE_CONT**: Continue the execution of a process that is currently halted.

Call `PTRACE_ATTACH`, the kernel will send the process a `SIGSTOP` to pause its execution.

### Launching and Attaching to Processes

```bash
# Launching a named program and attaching to it by running:
ldb <program name>
# Attach to an existing process
ldb -p <pid>
```


#### Attaching Function
Same as `gdb` and `lldb`.

```shell
while sleep 5; do echo "I'm alive!"; done&
[1] 1247
$ tools/ldb -p 1247
```

每次需要输入c，才会输出一次"I'm alive!"。

调试器调用 ptrace(PTRACE_CONT, pid, nullptr, nullptr)，让 bash 进程继续执行。
bash 继续运行循环，执行 echo "I'm alive!"，打印出一行输出。
echo 完成后，bash 收到 echo进程的 SIGCHLD 信号。
因为 bash 被 ptrace 跟踪，这个 SIGCHLD 导致 bash 暂停。
调试器的 waitpid 捕获到这个暂停状态，程序回到等待用户输入的状态。
结果就是：你只看到一次 "I'm alive!"，然后进程又停了，需要再次输入 continue。


## Registers

```shell
register read <register name>
register read all
register write <register name> <value>
```

### FR

`st_space[32]` for st/mm

`xmm_space[64]` for xmm

## Assembly

The syscall ID goes in rax; subsequent arguments go in rdi,
rsi, rdx, r10, r8, and r9; and the return value of the syscall is stored in rax.


```ass

movq %mm0, %rsi
# movq: "Move Quadword" 指令，用于移动 64 位（8 字节）的数据。


leaq hex_format(%rip), %rdi
# leaq: "Load Effective Address Quadword" 指令。它计算源操作数的内存地址，并将该地址（而不是地址处的内容）加载到目的寄存器中。
# hex_format(%rip): 源操作数。这是 RIP 相对寻址。它表示 "标签 hex_format 的地址相对于当前指令指针（%rip）的偏移量"。这是一种在位置无关代码 (PIC) 中访问全局或静态数据的常用方式。`hex_format` 很有可能是一个指向数据段中字符串的标签。
# %rdi: 目的操作数。通用 64 位寄存器。
# 含义：计算标签 `hex_format` 的有效内存地址，并将这个地址存储到 rdi 寄存器中。
#       根据 System V AMD64 ABI，rdi 寄存器用于传递函数的 *第一个* 整数/指针参数。
#       结合上一条指令和接下来的 `call printf`，我们可以推断 `hex_format` 指向的是一个格式化字符串，用于 `printf` 函数，很可能是类似 `"%016llx\n"` 或 `"%p\n"` 的形式，以便将 rsi 中的 64 位值以十六进制格式打印出来。

movq $0, %rax
# movq: 移动 64 位数据。
# $0: 源操作数。一个立即数 0。
# %rax: 目的操作数。通用 64 位寄存器。
# 含义：将立即数 0 移动到 rax 寄存器中。
#       根据 System V AMD64 ABI，对于可变参数函数（如 `printf`），rax 寄存器需要包含传递给该函数的 *向量寄存器* (XMM/YMM/ZMM) 的数量。因为这里我们没有通过向量寄存器传递浮点或向量参数（mm0 不是调用约定中定义的向量寄存器），所以需要将 rax 设置为 0。

call printf@plt
# call: 调用指令。它将下一条指令的地址压入栈中（作为返回地址），然后跳转到目标地址执行。
# printf@plt: 目标地址。这表示调用 C 标准库中的 `printf` 函数。`@plt` 后缀表示这是通过过程链接表 (Procedure Linkage Table) 进行的调用。PLT 是动态链接中用于解析共享库函数实际地址的一种机制。
# 含义：调用 `printf` 函数。根据之前的设置：
#        - rdi (第一个参数) 指向格式化字符串 (位于 hex_format)。
#        - rsi (第二个参数) 包含原始 mm0 寄存器的值。
#        - rax (向量寄存器计数) 为 0。
#       因此，这条指令会按照 `hex_format` 指定的格式打印出 mm0 的内容。

movq $0, %rdi
# movq: 移动 64 位数据。
# $0: 源操作数，立即数 0。在 C 语言上下文中，通常表示 NULL 指针。
# %rdi: 目的操作数。
# 含义：将 rdi 寄存器设置为 0。这是为下一个函数调用 `fflush` 准备第一个参数。

call fflush@plt
# call: 调用指令。
# fflush@plt: 调用 C 标准库中的 `fflush` 函数，同样通过 PLT。
# 含义：调用 `fflush` 函数。根据之前的设置，rdi (第一个参数) 为 0 (NULL)。
#       当 `fflush` 的参数为 NULL 时，它会刷新 *所有* 打开的输出流的缓冲区（例如标准输出 stdout）。这确保了之前 `printf` 的输出能立即显示出来，特别是当 stdout 是行缓冲或全缓冲，并且 `printf` 的格式化字符串末尾没有换行符 `\n` 时，这个调用尤其有用。

trap
# trap: 这通常是 `int3` 指令的一个别名，即中断 3。
# 含义：产生一个断点中断。如果程序在调试器下运行，这会导致程序暂停执行，让开发者可以检查状态。如果程序正常运行（没有调试器附加），这通常会导致程序异常终止，并可能显示 "Trace/breakpoint trap" 之类的消息。这常用于调试目的，在代码的特定点强制停止。
```

## Software Breakpoints

Hardware breakpoints involve **setting architecture-specific registers** to
produce breaks for you, whereas software breakpoints involve **modifying the
machine instructions** in the process’s memory.

- Hardware breakpoints trigger breaks if a given address is executed, written to, or read from.
- Software breakpoints trigger breaks on execution only.

Use `ptrace` to read and write memory. On x64, we can overwrite the instruction at the address with the `int3` instruction.

When the processor executes the `int3` instruction, it passes control to the breakpoint interrupt handler, which-in the case of Linux-signals the p

`WaitOnSignal()` can listen for signals being sent to the inferior process by the argument `waitStatus`。

### Logical Breakpoint `ldb::Breakpoint` and Physical Breakpoint `ldb::BreakpointSite`
It may be associated with several locations. For example, set a breakpoint in the function `ToString()`. There are many overloads of `ToString()` with differenct types, so we need to create multiple physical breakpoints for each overload.

```bash
break set 0xcafecafe
continue
```

### PIE

These executables don’t expect to be loaded at a specific memory
address; they can be loaded anywhere and still work. As such, memory ad-
dresses within PIEs aren’t absolute virtual addresses, they’re offsets from the
start of the final load address of the binary.

#### objdump

In other words, 0x115b doesn’t mean “virtual address 0x115b,” it means “0x115b bytes away from where the
binary was loaded.” We’ll refer to these addresses as **file addresses**. We’ll also
sometimes need to refer to offsets from the start of the object file on disk,
which we’ll call **file offsets**.

```asm
.text:
...
0000000000001149 <main>:
#include <cstdio>

    1149:       f3 0f 1e fa             endbr64
    114d:       55                      push   %rbp
    114e:       48 89 e5                mov    %rsp,%rbp
    1151:       48 8d 05 ac 0e 00 00    lea    0xeac(%rip),%rax        # 2004 <_IO_stdin_used+0x4>
    1158:       48 89 c7                mov    %rax,%rdi
    115b:       e8 f0 fe ff ff          call   1050 <puts@plt>
    1160:       b8 00 00 00 00          mov    $0x0,%eax
    1165:       5d                      pop    %rbp
    1166:       c3  
```



#### /proc/<pid>/maps
The first part is the address range, followed
by the read/write/execute permissions of the region. Next comes the file
offset of the segment.

```sh
556e2e531000-556e2e532000 r--p 00000000 08:10 129448  /path/to/run_endlessly
556e2e532000-556e2e533000 r-xp 00001000 08:10 129448  /path/to/run_endlessly
556e2e533000-556e2e534000 r--p 00002000 08:10 129448  /path/to/run_endlessly
556e2e534000-556e2e535000 r--p 00002000 08:10 129448  /path/to/run_endlessly
556e2e535000-556e2e536000 rw-p 00003000 08:10 129448  /path/to/run_endlessly
```

#### readelf

```sh
readelf -S test/targets/hello_ldb

Section Headers:
 [Nr] Name              Type             Address           Offset
      Size              EntSize          Flags  Link  Info  Align
[16] .text             PROGBITS         0000000000001060  00001060
       0000000000000107  0000000000000000  AX       0     0     16
```

If the file offset and file ad-
dresses of the .text are different for your executable, you’ll need to subtract the file offset from the file address to calculate the section load bias for that section, then subtract this load bias from the file address of the call
instruction to calculate where that instruction lives inside the ELF file.



```
section_load_bias = fileaddr - offset
offset_call = fileaddr_call - section_load_bias
                   mem                      ELF
                |       |               |       |
    load bias   |       |         offset|  .text|
                |       |               |       |
      fileaddr  |       |               |       |
                |       |               |       |
                |       |               |       |
                |       |               |       |
```


   555555555000-555555556000 r-xp 00001000 08:20 203814    # .text section
虚拟地址 = RVA + 加载基址 - 节偏移
= 0x115b + 0x555555555000 - 0x1000
为什么这样计算：
从 /proc/pid/maps 可以看到:
0x555555555000 是代码段（.text）的加载基址
0x1000 是文件偏移（第三列）
objdump 显示的地址 0x115b:
这是相对虚拟地址（RVA）
是相对于节的开始位置的偏移

计算过程:
要得到实际的虚拟地址，需要：
从加载基址开始（0x555555555000）
加上指令的 RVA（0x115b）
减去节的文件偏移（0x1000）
这样就能得到指令在内存中的真实地址

为什么要减去 0x1000:
因为 objdump 显示的地址包含了节偏移
而加载基址已经考虑了这个偏移
如果不减去，就会重复计算这个偏移


### ELF Address

objdump -s test/targets/hello_ldb结果中的指令地址 = 其所属段基地址(p_vaddr) + 指令在段内的偏移量

```asm
.text:
...
0000000000001149 <main>:
#include <cstdio>

    1149:       f3 0f 1e fa             endbr64
    114d:       55                      push   %rbp
    114e:       48 89 e5                mov    %rsp,%rbp
    1151:       48 8d 05 ac 0e 00 00    lea    0xeac(%rip),%rax        # 2004 <_IO_stdin_used+0x4>
    1158:       48 89 c7                mov    %rax,%rdi
    115b:       e8 f0 fe ff ff          call   1050 <puts@plt>
    1160:       b8 00 00 00 00          mov    $0x0,%eax
    1165:       5d                      pop    %rbp
    1166:       c3  
```

#### 段基地址(p_vaddr)：

- 在ELF文件的程序头表(Program Header Table)中定义
- 表示该段预期被加载到内存中的虚拟起始地址
- 可通过readelf -l命令查看
#### 反汇编中的指令地址：

- 通过objdump -d或其他反汇编工具显示
- 这些地址是链接器为指令分配的虚拟内存地址
- 已经包含了段基地址的偏移计算

#### 相对偏移量计算

要获取指令在段内的相对偏移量：偏移量 = 指令地址 - 段基地址(p_vaddr)
这个偏移量在文件中是固定的，不会随运行时重定位而改变
示例
假设一个可执行文件中：

包含代码的LOAD段p_vaddr = 0x400000
objdump显示某函数起始地址为0x401240
则：

获取实际加载基址：
- 非PIE可执行文件：ELF文件中的地址即运行时地址
- PIE可执行文件：需通过进程映射获取随机化基址

该函数在段内的偏移量为: 0x401240 - 0x400000 = 0x1240
如果运行时该段被加载到0x7ff000000000，则函数的实际内存地址为: 0x7ff000000000 + 0x1240 = 0x7ff000001240
不同可执行文件类型区别
- 标准可执行文件：p_vaddr通常是较高的固定地址（如0x400000）
- PIE可执行文件：p_vaddr通常很小（甚至为0），主要依赖运行时重定位
- 共享库(.so)：类似PIE，使用相对地址，在运行时动态重定位


### Hardware breakpoint

x86架构提供4个调试寄存器（DR0到DR3）用于设置硬件断点
这意味着同一时间最多只能设置4个硬件断点

DR6的低4位（0-3位）分别对应DR0-DR3：

位  3   2   1   0
    |   |   |   |
    |   |   |   +-- DR0断点触发标志
    |   |   +------ DR1断点触发标志
    |   +---------- DR2断点触发标志
    +-------------- DR3断点触发标志

#### Condition Bits
00b - 仅指令执行（执行断点）
01b - 仅数据写入（写入监视点）
10b - I/O读写（通常不支持）
11b - 数据读写（读写监视点）


## Reloacting Symbol References

### Function Call

*由于PIE，0x1000是相对于运行时加载地址的偏移量(file address)*

```
# 例如：
0x1000: e8 xx xx xx xx    # call指令 (xx xx xx xx 需要替换new_value)
0x1005: ...               # 下一条指令

# CPU执行call时的行为：
1. PC = 0x1005           # PC指向下一条指令
2. 目标的运行时地址 = PC + 偏移值   # 使用下一条指令地址计算

# 如果我们要跳转到0x2000：
目标的运行时地址 = PC + 偏移值(new_value)
0x2000 = 0x1005 + 偏移值
偏移值 = 0x2000 - 0x1005
      = 0xffb

# 但重定位公式是：
new_value = S + A - P
         = 0x2000 + (-4) - 0x1001
         = 0x2000 - 0x1005
         = 目标地址 - 下一条指令地址
         = 0xffb

# S: 符号的运行时地址
# A: addend值
# P: 需要重定位的引用符号的运行时地址(e8的下一个字节的地址:0x1001)
```