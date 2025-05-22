# LDB

LDB is a debugging tool for C++ programs.

cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake

# Attaching to a Process

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


# 寄存器

- 包含通用寄存器、x87 寄存器、MMX 寄存器、SSE 寄存器、SSE2 寄存器、AVX 寄存器、AVX-512 寄存器以及调试寄存器
- x64 架构拥有 16 个 64 位通用寄存器 (GPRs)
- ABI规定了寄存器的具体用途，特别是在函数调用时（参数如何传递，返回值在哪里，哪些寄存器需要保存等）。Linux 使用 System V ABI。
- 调用者保存的寄存器可以在函数内部安全地覆写。如果被调用者保存的寄存器要在函数内修改，则必须在函数开始时保存它们，并在函数结束时恢复
    - 调用约定：
        - 调用者保存: 如果函数 A 调用函数 B，函数 A 在调用 B 之前不需要担心 B 会修改这些寄存器；如果 A 自己需要这些寄存器的值在 B 返回后保持不变，A 负责在调用 B 之前保存它们。rax, rcx, rdx, rsi, rdi, r8-r11 通常是调用者保存的。
        - 被调用者保存 (Callee-saved): 如果函数 B 要使用这些寄存器，B 必须在修改它们之前先保存其原始内容，并在返回给 A 之前恢复它们。rbx, rbp, r12-r15 通常是被调用者保存的。
- 浮点寄存器x87包括八个 80 位寄存器，名为 st0 到 st7,`unsigned int st_space[32]`。
- SIMD (单指令多数据)MMX包括八个 64 位寄存器，名为 mm0 到 mm7，映射到与 x87 寄存器相同的内存区域，不能与 x87 浮点运算同时使用。
- 流式 SIMD 扩展 (SSE)，改进了 MMX。该指令集添加了八个 128 位寄存器，名为 xmm0 到 xmm7,SSE2 后来扩展了该集合，添加了寄存器 xmm8 到 xmm15, `unsigned int xmm_space[64]`。
- 调试寄存器允许设置硬件断点（当执行到达特定地址时暂停）和数据观察点（当特定内存地址被读取或写入时暂停），这些是由 CPU硬件直接支持的，比软件实现的断点/观察点效率更高。

# x64 Assembly

## dup2
`dup2(int oldfd, int newfd)`系统调用
`dup2` 的核心功能是复制一个现有的文件描述符 (`oldfd`) 到另一个指定的文件描述符 (`newfd`)。
- 如果 `newfd` 已经打开，那么首先自动关闭 `newfd` 指向的文件。这个关闭操作是原子性的，与后续的复制操作一起完成。
- 在确保 `newfd` 可用（或已被关闭）之后，dup2 会使 `newfd` 指向与 `oldfd` 相同的打开文件表项 (open file table entry)。这意味着 `newfd` 现在变成了 `oldfd` 的一个副本或别名。它们共享相同的文件状态标志（如文件偏移量、读写模式、访问模式等）。

## 文件结构
### 进程文件描述符表

每个进程都有自己的文件描述符表。这是一个数组（或类似结构），其索引就是文件描述符。表中的每个条目是一个指针，指向系统级的打开文件表 (Open File Table) 中的一个表项。

`dup2(oldfd, newfd)` 会修改当前进程的文件描述符表。
它将 `newfd` 这个索引位置的指针，设置为与 `oldfd` 索引位置相同的指针值，即让它们都指向同一个打开文件表项。
如果 `newfd` 之前指向另一个打开文件表项，那么对那个旧表项的引用计数会减少（如果这是最后一个指向它的文件描述符，并且没有其他进程共享它，那么该表项可能被释放，相应的文件也会被关闭）。

### 系统打开文件表

内核维护，包含了系统中所有当前打开的文件的信息。
每个表项代表一个打开的文件实例，并存储：
- 文件状态标志 (File status flags: e.g., O_RDONLY, O_WRONLY, O_APPEND, O_NONBLOCK 等，这些是在 open() 时设置的)。
- 当前文件偏移量 (Current file offset: 即下一次读写操作将在文件的哪个位置开始)。
- 指向该文件对应 v-node (或 inode) 表项的指针。
- 一个引用计数，表示有多少个进程文件描述符表条目指向这个打开文件表项。

`dup2` 不会创建新的打开文件表项。它只是让 `newfd` 指向 `oldfd` 已经指向的那个现有的打开文件表项。
`因此，oldfd` 和 `newfd` 将共享相同的文件状态标志和文件偏移量。这意味着：
如果通过 `newfd` 读取了文件内容，文件偏移量会前进。随后通过 `oldfd` 读取时，会从新的偏移量开始。
如果通过 `oldfd` 修改了文件状态标志（例如，使用 fcntl 设置 O_APPEND），那么通过 newfd 访问文件时也会受到这个新标志的影响。

### i-node 表:
也称为 v-node 表。存储了关于文件本身的元数据，如文件类型、权限、所有者、大小、时间戳以及指向数据块的指针等。一个 i-node 代表磁盘上的一个文件或目录。

dup2 对 i-node 表没有直接影响。它操作的是文件描述符和打开文件表项的层面。多个打开文件表项（可能来自不同进程，或者同一进程的不同文件描述符通过多次 `open` 同一个文件）可以指向同一个 i-node。

## 发起系统调用

系统调用号放入 `rax`；后续参数依次放入 `rdi, rsi, rdx, r10, r8` 和 `r9`,其余放入栈中；系统调用的返回值存储在 `rax` 中。

为了向进程自己发送一个 `SIGTRAP` 信号:
- 将 `kill` 系统调用的编号62放入 `rax`.
- 将正在运行进程的 PID 放入 `rdi`。
- 将 `SIGTRAP` 的信号编号 (5) 放入 `rsi`。
- 执行 `syscall` 指令。


```ass
# movq: "Move Quadword" 指令，用于移动 64 位（8 字节）的数据。
movq %mm0, %rsi


# leaq: "Load Effective Address Quadword" 指令。它计算源操作数的内存地址，并将该地址（而不是地址处的内容）加载到目的寄存器中。
# hex_format(%rip): 源操作数。这是 RIP 相对寻址。它表示 "标签 hex_format 的地址相对于当前指令指针（%rip）的偏移量"。这是一种在位置无关代码 (PIC) 中访问全局或静态数据的常用方式
leaq hex_format(%rip), %rdi

# 可变参数函数期望 rax 包含参与参数传递的向量寄存器的数量
movq $0, %rax


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

# 软件断点

## 如何实现

修改目标进程内存中目标指令的字节码，通过用 `int3` 指令覆盖该地址处的指令,当处理器执行 `int3` 指令时，它会将控制权传递给断点中断处理程序，在 Linux 的情况下，该处理程序会向进程发送一个 `SIGTRAP` 信号。

## 如何通知调试器发生了中断

当子进程因为执行 `int3` 指令而收到 `SIGTRAP` 信号时，它会暂停执行，并且内核会通知正在跟踪它的父进程（调试器）,`wait_on_signal` 函数，等待子进程的状态改变。

## 断点位置 

设置一个逻辑断点时，该断点可能与多个实际位置相关联。例如，假设在函数 `to_string` 中设置了一个断点。`to_string` 有许多重载版本，因此要为每个重载版本创建物理断点。称前者为 `ldb::breakpoint`（逻辑断点），后者为 `ldb::breakpoint_site`（物理断点位置）来区分用户级别的逻辑断点和实际的物理断点。

```
objdump -d hello_sdb
```

```
0000000000001140 <main>:
    1140:       55                      push   %rbp
    1141:       48 89 e5                mov    %rsp,%rbp
    1144:       48 8d 3d b9 0e 00 00    lea    0xeb9(%rip),%rdi        # 2004 <_IO_stdin_used+0x4>
    114b:       e8 e0 fe ff ff          call   1030 <puts@plt>
    1150:       31 c0                   xor    %eax,%eax
    1152:       5d                      pop    %rbp
    1153:       c3                      ret
```

`0000000000001149`等是指令在内存中的（通常是相对于**程序加载基址**的）虚拟地址。这些地址在每次编译或链接时，或者由于 *ASLR (地址空间布局随机化)* 的存在，可能会有所不同。

## 位置无关可执行文件 (PIE)

### file_addr
PIE 内部的内存地址(`file_addr`)不是绝对虚拟地址，它们是相对于二进制文件最终加载地址起始点的偏移量。`0x114b` 并不意味着“虚拟地址 `0x114b`”，而是意味着“距离二进制文件被加载的起始位置 `0x114b` 字节远的地方”。

加载一个 PIE 时，会为其选择随机的基地址，然后将程序的各个段加载到从这个基地址开始的相应偏移处。

### file_offset

磁盘文件的偏移量。这与加载到内存后的`file_addr`可能不同，因为 ELF 文件的加载过程会将不同的段映射到内存中，并且内存布局可能与磁盘文件布局不完全一致（例如，对齐要求、某些段不加载等）。
These executables don’t expect to be loaded at a specific memory
address; they can be loaded anywhere and still work. As such, memory ad-
dresses within PIEs aren’t absolute virtual addresses, they’re offsets from the
start of the final load address of the binary.

### /proc/<pid>/maps
每一行都是一个映射的内存区域，它们对应于目标文件的不同段。第一部分是地址范围，后面是该区域的读/写/执行权限。接下来是段的文件偏移量(`file_offset`)，设备号 (主:次)， i-node 号，路径。

```sh
556e2e531000-556e2e532000 r--p 00000000 08:10 129448  /path/to/run_endlessly
556e2e532000-556e2e533000 r-xp 00001000 08:10 129448  /path/to/run_endlessly
556e2e533000-556e2e534000 r--p 00002000 08:10 129448  /path/to/run_endlessly
556e2e534000-556e2e535000 r--p 00002000 08:10 129448  /path/to/run_endlessly
556e2e535000-556e2e536000 rw-p 00003000 08:10 129448  /path/to/run_endlessly
```

### readelf

```
smooth@life:~/Code/ldb/build/test$ readelf -S targets/hello_ldb 
There are 38 section headers, starting at offset 0x4350:

Section Headers:
  [Nr] Name              Type             Address           Offset
       Size              EntSize          Flags  Link  Info  Align

  [15] .text             PROGBITS         0000000000001050  00001050
       0000000000000104  0000000000000000  AX       0     0     16
```

`Address`: 当文件被加载到内存时，该节期望被加载到的（相对）虚拟地址。对于 PIE，这是`file_addr`。
`Offset`: 该节在磁盘上的 ELF 文件中的起始字节偏移量`file_offset`。
- 如果 `Address` 和 `Offset` 不同：这可能发生在 ELF 文件中有其他非加载段占用了文件前面的空间，或者由于对齐等原因，节在文件中的物理布局和加载到内存后的逻辑布局存在差异。
- 加载偏差 (Load Bias) = `Address` (内存中的相对虚拟地址) - `Offset` (文件中的偏移)。
- 指令在 ELF 文件中的偏移 = objdump 文件地址 - 加载偏差。这个计算结果是指令相对于 ELF 文件中该节起始的真实偏移。
- 运行时虚拟地址 = 段的运行时加载基地址 + (指令的`file_addr` - 段在ELF中定义的起始虚拟地址:从 readelf -S 的 `Address` 字段)
```sh
readelf -S test/targets/hello_ldb

Section Headers:
 [Nr] Name              Type             Address           Offset
      Size              EntSize          Flags  Link  Info  Align
[16] .text             PROGBITS         0000000000001060  00001060
       0000000000000107  0000000000000000  AX       0     0     16
```



# Hardware breakpoint

x86架构提供4个调试寄存器（DR0到DR3）用于设置硬件断点
这意味着同一时间最多只能设置4个硬件断点

x64 上的调试寄存器（Debug Registers, DR）共有 16 个，命名为 DR0–DR15，作用如下：
- DR0–DR3：最多 4 个硬件断点/监视点的地址。
- DR4, DR5：已废弃（分别是 DR6、DR7 的别名）。
- DR6：调试状态寄存器。
- DR7：调试控制寄存器。
    - 0–7 位：每两位控制一个断点的启用状态
        - 偶数位：Local（进程级）
        - 奇数位：Global（系统级，但 Linux 上与 Local 行为相同）
    - 16–17、20–21、24–25、28–29 位：4 组条件码，决定“执行/写/读写/IO”哪种行为触发
        - 00b：仅当指令被执行触发（硬件断点）
        - 01b：仅数据写入触发
        - 10b：I/O 读写触发（基本用不到）
        - 11b：数据读或写触发
    - 18–19、22–23、26–27、30–31 位：4 组大小码，决定监视点监视的字节数。
        - 00b：1 字节 01b：2 字节 10b：8 字节 11b：4 字节，对于执行断点，大小必须设为 1 字节（00b）
- DR8–DR15：保留给处理器内部使用。


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

# Elf

## section

视角：链接时视角 (Link-time view)。主要供链接器 (ld) 使用。

内容：包含程序的不同组成部分，如：
 - .text: 可执行指令（代码）。
 - .data: 已初始化的全局和静态变量。
 - .bss: 未初始化的全局和静态变量（在文件中不占空间，加载时分配并清零）。
 - .rodata: 只读数据（如字符串字面量）。
 - .symtab: 符号表（函数名、变量名等）。
 - .strtab: 字符串表（存储节名、符号名等字符串）。
 - .shstrtab: 节头部字符串表（存储节的名称）。
 - .debug_*: DWARF 调试信息节。
 - .interp: 指定程序解释器（通常是动态链接器，如 /lib64/ld-linux-x86-64.so.2）。

属性：每个节都有名称、类型、标志（如可写、可执行、可分配）、在文件中的偏移量、大小、在内存中的（相对）地址等。

节头部表 (Section Header Table)：ELF 文件中有一个表，描述了所有节的信息。readelf -S 或 readelf --sections 显示此表。

## segment


- 视角：执行时视角 (Execution-time view)。主要供加载器（操作系统内核的一部分，或动态链接器）使用，用于将文件加载到内存并准备执行。
- 内容：描述了如何将 ELF 文件的某些部分（通常是一个或多个节的组合）映射到进程的虚拟内存空间。
- 属性：每个段有类型（如 PT_LOAD 表示可加载到内存的段，PT_INTERP 表示解释器路径，PT_PHDR 表示程序头部表自身的位置和大小）、在文件中的偏移量、大小、在内存中的虚拟地址、物理地址（通常与虚拟地址相同或未使用）、内存大小（可能大于文件大小，例如 .bss 段）、标志（如可读 R、可写 W、可执行 E）和对齐要求。
- 程序头部表 (Program Header Table)：ELF 文件中（对于可执行文件和共享库）有一个程序头部表，描述了所有段的信息。readelf -l 或 readelf --segments 显示此表。
- 段没有名称：与节不同，段通常没有名称，而是通过类型和属性来区分。

## 结构

### ELF 头 (ELF Header)：
- 位于每个 ELF 文件的开头。
- 包含文件的高级信息，例如：
   - e_ident：一个字节数组，其中包含了"魔数"（0x7F 后跟 ELF 三个字符），用于标识文件为 ELF 格式。还包含关于文件的类别（32位/64位）、字节序等信息。
   - e_type：文件类型（如可执行文件、可重定位文件、共享对象文件或核心转储文件）。
   - e_machine：目标体系结构（如 EM_X86_64 表示 x86-64）。
   - e_version：ELF 版本，通常为 1。
   - e_entry：程序的入口点虚拟地址（如果文件是可执行的）。
   - e_phoff：程序头表 (Program Header Table) 在文件中的偏移量。
   - e_shoff：节头表 (Section Header Table) 在文件中的偏移量。
   - 以及其他关于头部大小、程序头和节头表中条目数量和大小的信息。

### 程序头表 (Program Header Table)：

- 紧跟在 ELF 头之后（通常是这样，但具体位置由 e_phoff 决定）。
- 描述了零个或多个段 (segments)。段是操作系统加载和运行程序时所关注的单元。例如，一个代码段、一个数据段等。
- 每个程序头条目（类型为 Elf64_Phdr 或 Elf32_Phdr）包含段的类型、在文件中的偏移量、在内存中的虚拟地址、物理地址（如果相关）、段在文件和内存中的大小、以及段的标志（如读/写/执行权限）。
- 这构成了 ELF 文件的执行视图 (Execution View)。

### 节头表 (Section Header Table)：

- 描述了文件中的零个或多个节 (sections)。节是链接器和开发工具（如反汇编器、调试器）所关注的数据单元。例如，.text（代码）、.data（已初始化数据）、.bss（未初始化数据）、.rodata（只读数据）、.symtab（符号表）、.strtab（字符串表）等。
- 每个节头条目（类型为 Elf64_Shdr 或 Elf32_Shdr）包含节的名称（作为字符串表中的索引）、类型、属性、在内存中的虚拟地址（如果可加载）、在文件中的偏移量、大小等。
e_shstrndx 字段在 ELF 头中指定了包含节名称字符串的字符串表所在的节的索引。
- 这构成了 ELF 文件的链接视图 (Linking View)。

### 数据 (Data)：

- 文件的其余部分由程序头表和节头表所描述的段和节的数据组成。
- 同一个字节区域可以属于某个段，同时也属于一个或多个节。如图 11-1 所示，段通常包含一个或多个完整的节。

简而言之，ELF 文件通过 ELF 头提供元信息，通过程序头表指导如何将文件内容加载到内存形成进程映像（执行视图），并通过节头表提供详细的内部结构信息以支持链接、调试等操作（链接视图）。


## 字符串表

字符串表是一系列以空字符（null，\0）结尾的字符串的集合。它允许 ELF 文件的其他部分通过字节偏移量来引用特定字符串。通常，一个 ELF 文件包含两个字符串表：
1. 一般字符串表：位于 `.strtab` 节中，存储一般用途的字符串
2. 节名字符串表：位于 `.shstrtab` 节中，专门用于存储节名称
3. 某些文件可能还有 `.dynstr` 表，包含用于动态链接的字符串

### 一般字符串表(`.strtab`)

有些 ELF 文件可能只有一个精简版的 `.dynstr` 节来代替 `.strtab` 节

## 文件地址，文件偏移和虚拟地址

1. 从对象文件开始的绝对偏移（对应 sdb::file_offset 类型）
2. ELF 文件中指定的虚拟地址（对应 sdb::file_addr 类型，包括section header中的sh_offset偏移）
3. 执行程序中的实际虚拟地址（对应 sdb::virt_addr 类型）

ELF 文件中连续的节在内存中不一定连续映射；它们之间可能存在间隙。当对应于这些节的段具有不同的内存权限时，通常会发生这种情况。例如，包含 .data 节的段应该被映射为可读写，但包含 .text 节的段通常被映射为可读可执行。Linux 为内存页分配权限，x64 上这些页大小为 4,096 字节，因此如果权限不同的节没有对齐到 4,096 字节，系统必须在它们之间加载间隙。

因此，ELF 文件指定了每个段加载的文件地址，为了使段之间的交叉引用正常工作，整个 ELF 文件的文件地址和实际虚拟地址之间的差异永远只是一个偏移量，称为加载偏移（load bias）。ELF 文件中指定的间隙和加载段之间的间隙将始终相同。

## 符号表

符号表包含程序全局实体（如变量和函数）的链接相关信息。与可能从调试信息中获得的高级源代码细节不同，符号表包含更基本的内容，例如：
- 实体的大小（例如，对象大小或函数机器码中的字节数）
- 实体是否可供其他可能与其链接的 ELF 文件访问
- 实体所属的类别（例如，函数、变量、ELF 节或文件）

一个 ELF 文件可能有两个符号表：
1. 完整符号表：名为 .symtab，其节头 sh_type 成员为 SHT_SYMTAB。
2. 缩写符号表：名为 .dynsym，其节类型为 SHT_DYNSYM，仅包含动态链接所需的符号集。

每个 ELF 文件最多只能有其中一种，也可能根本没有符号表。许多开发者会剥离可执行文件中的符号以节省空间。

### Elf64_Sym

```C
typedef struct {
    Elf64_Word    st_name;  // 符号名称（字符串表中的偏移量）
    unsigned char st_info;  // 符号的类型（例如函数、变量）和绑定（例如局部、全局）
    unsigned char st_other; // 符号的可见性
    Elf64_Half    st_shndx; // 符号定义在哪个节中
    Elf64_Addr    st_value; // 符号的值（对于可重定位文件，这通常是相对于 st_shndx 所指定节的偏移量。对于可执行文件或共享对象，这通常是符号的虚拟地址。)
    Elf64_Xword   st_size;  // 符号的大小（以字节为单位）
} Elf64_Sym;
```

## 辅助向量 (Auxiliary Vectors)

位于 `/proc/<pid>/auxv` 中的条目是二进制编码的 64 位整数对。
辅助向量是内核在启动用户进程时传递给该进程的一系列键值对。这些键值对提供了关于进程环境和配置的各种信息。其中，AT_ENTRY 这个键对应的值就是程序被加载到内存后的实际入口点地址。
ELF 文件头本身 (Elf64_Ehdr::e_entry) 也记录了一个入口点地址，这是链接器假设的、相对于文件开头的入口点（或者说，相对于期望的加载基址的入口点）。
通过 实际入口点 (AT_ENTRY) 减去 ELF 文件头中的入口点 (e_entry)，就可以计算出整个 ELF 文件在内存中的加载基址偏移 (load bias)。

# Dwarf

- 它在编译后的二进制文件和产生它的源代码之间建立了桥梁。
- 核心功能：
    - 将机器指令映射回源代码行。
    - 在运行的进程中定位函数和变量的地址（基于源代码中的名称）。
    - 描述程序中定义的各种数据类型（类、结构体、枚举、基本类型等）。
    - 支持查找变量的值、解析表达式等。

## 所在section

DWARF 与 ELF 的关系：DWARF 信息通常嵌入在 ELF 文件的特定节区中。这些节区有标准的名称（通常以 .debug_ 开头）。

- .debug_info: 包含了描述程序实体（如编译单元、函数、变量、类型、命名空间等）的主要数据。这些数据以 DWARF 信息条目 (Debugging Information Entries, DIEs) 的形式组织。
- .debug_abbrev: 包含缩写数据，用于定义 .debug_info 中 DIE 的结构和属性的编码方式，以减小 .debug_info 的体积。每个 DIE 都有一个指向 .debug_abbrev 中某个缩写声明的引用。
- .debug_aranges: 包含一个已编译代码地址范围到相应编译单元调试信息的快速查找表。可以帮助调试器快速定位与给定代码地址相关的 .debug_info 部分。
- - .debug_line: 包含行号表程序 (line number program)。这是一个状态机，用于将机器指令地址映射回源代码文件名和行号，并标记语句边界、基本块边界等。对于实现按源代码行单步执行至关重要。
.debug_str: DWARF 使用的字符串表，存储各种名称（如变量名、函数名、类型名等）。其格式与 ELF 的字符串表（如 .strtab, .shstrtab）类似。DIE 中的某些属性会包含指向此表中字符串的偏移量。
- .debug_loc (DWARF 4 及更早) / .debug_loclists (DWARF 5): 描述变量在不同代码范围内的存储位置（例如，有时在寄存器中，有时在栈上的不同位置）。
- .debug_ranges (DWARF 4 及更早) / .debug_rnglists (DWARF 5): 当一个 DIE 描述的实体（如函数）对应的代码地址不是连续的时（例如，由于优化导致代码分散），这个节用于描述这些不连续的地址范围。

### .debug_info

#### DWARF 信息条目 (DIE - Debugging Information Entry)

1. 每个 DIE 描述程序中的一个实体，如：
    - 编译单元 (DW_TAG_compile_unit)
    - 命名空间 (DW_TAG_namespace)
    - 类/结构体/联合体 (DW_TAG_class_type, DW_TAG_structure_type, DW_TAG_union_type)
    - 函数/方法 (DW_TAG_subprogram)
    - 变量 (DW_TAG_variable)
    - 参数 (DW_TAG_formal_parameter)
    - 基本类型 (DW_TAG_base_type)
2. 树状结构：DIEs 在每个编译单元内部被组织成一棵树。例如，一个函数的 DIE 可能是编译单元 DIE 的子节点，而该函数的参数和局部变量的 DIE 又是函数 DIE 的子节点。
3. dwarfdump 工具。输出示例显示：
- level，offset: 每行 DIE 的开头。level 是嵌套深度，offset 是该 DIE 在 .debug_info 节中的字节偏移。
- 标签 (Tag)：DW_TAG_xxx，指明了 DIE 所描述的实体类型。
- 属性 (Attributes)：DW_AT_yyy value，每个 DIE 包含一个或多个属性，每个属性由一个类型 (DW_AT_yyy) 和一个值组成，描述了该实体的具体特性。例如：
    - DW_AT_name: 实体的名称（如函数名、变量名、文件名）。
    - DW_AT_language: 源代码语言（如 C++）。
    - DW_AT_comp_dir: 编译目录。
    - DW_AT_low_pc 和 DW_AT_high_pc: 实体（如函数或编译单元）对应的机器码在内存中的起始和结束地址（通常是文件地址或相对地址）。
    - DW_AT_decl_file, DW_AT_decl_line, DW_AT_decl_column: 实体在源代码中声明的文件、行号和列号。
    - DW_AT_sibling: 指向当前 DIE 在树中的下一个兄弟节点的偏移量。这有助于解析器跳过当前 DIE 的所有子节点，直接处理下一个同级节点。

形式 (Form)：对于每个属性，DWARF 不仅定义了属性的类型（例如 DW_AT_name），还定义了该属性值的编码形式 (form)（例如 DW_FORM_string, DW_FORM_data1, DW_FORM_strp）。这个“形式”告诉解析器如何解释属性值在 .debug_info 节中的二进制表示。

#### 结构

.debug_info 节由一个或多个编译单元 (Compile Unit, CU) 的调试信息组成。每个 CU 的数据块都以一个编译单元头部 (Compile Unit Header) 开头。这个头部包含以下关键信息：
- unit_length (单元长度)：一个 4 字节或 12 字节（对于 DWARF64）的无符号整数。
    - 它表示从紧随其后的字段开始，到这个编译单元数据结束的总字节数。这个长度不包括 unit_length 字段本身的大小。
    - 如果 unit_length 的前 4 字节是 0xffffffff，则表示这是一个 DWARF64 格式的编译单元，实际长度由接下来的 8 个字节给出。本书专注于 32 位 DWARF，所以会检查这种情况并报错。
- version (DWARF 版本)：一个 2 字节无符号整数，指定了该编译单元调试信息所遵循的 DWARF 标准版本。本书关注 DWARF 版本 4。
- debug_abbrev_offset (缩写表偏移量)：一个 4 字节或 8 字节（对于 DWARF64）的无符号整数。它是一个偏移量，指向 .debug_abbrev 节中，该编译单元所使用的缩写表的起始位置。
- address_size (地址大小)：一个 1 字节无符号整数，指定了目标体系结构中地址的大小（以字节为单位）。例如，对于 x64 系统，这个值是 8。

每个编译单元的根节点是代表该编译单元本身的DIE。

### .debug_abbrev

缩写表的每个条目`abbrev`包含一个标签（tag）、一个编码该 DIE 是否有子节点（children）的位、一个属性类型列表，以及用于编码每个属性的形式（form）。然后，**DIE 本身只存储一个指向缩写表条目的索引，以及该 DIE 的属性值**。DWARF 将缩写表放置在 .debug_abbrev 节中。
- 缩写码 (Abbreviation Code)：一个唯一的数字，用于标识这个缩写声明。
- DIE 标签 (Tag)：例如 DW_TAG_subprogram。
- 是否有子节点 (Has Children)：一个标志位，指示这种类型的 DIE 是否可以有子 DIE。
- 属性规格列表：一个列表，对于该类型的 DIE 所拥有的每个属性，都指定了：
    - 属性名称（例如 DW_AT_name）。
    - 属性值的编码形式（例如 DW_FORM_strp）。

.debug_info 节中的每个 DIE 不再存储完整的属性类型和形式信息。
取而代之的是，每个 DIE 的开头存储一个缩写码。这个缩写码是一个索引，指向 .debug_abbrev 节中的某个缩写声明。
在缩写码之后，DIE 只按顺序存储该缩写声明所定义的那些属性的值。解析器在读取 DIE 时，会先读取缩写码，然后去 .debug_abbrev 查找对应的缩写声明，从而知道接下来应该如何解析属性值（即每个属性是什么类型，用什么形式编码的）。

.debug_abbrev 结构：
- 可能包含多个独立的缩写表。
- .debug_info 中的每个编译单元头部 (CU DIE) 会有一个属性 (通常是 DW_AT_abbrev_offset) 指向 .debug_abbrev 节中它所使用的那个缩写表的起始偏移量。
不同的编译单元可以指向同一个缩写表（如果它们的 DIE 结构相似），也可以指向不同的缩写表。


### .debug_ranges

用于表示非连续地址范围。包含一系列条目（三种类型，所有条目由2个整数组成）：

1. 常规
    - 相对于当前基地址的起始地址的偏移
    - 相对于当前基地址的结束地址的偏移
2. 基地址选择器
    - 所有位为1的数（表示是基地址选择器）
    - 基地址（所有后续条目都视为相对于此基地址的偏移，直到基地址再次被更改或列表结束）
3. 列表结束指示器
    - 0
    - 0

如果当前条目之前没有基地址选择器条目，则基地址被编码为引用该范围列表的 DIE 中的 DW_AT_low_pc 属性。这样的 DIE 将具有 DW_AT_low_pc 属性，但没有匹配的 DW_AT_high_pc 属性。

# 行号表`.debug_line`

行号表本身并不是一个简单的直接映射表，而是由一系列操作码（opcodes）组成，这些操作码通过驱动一个状态机来生成一个矩阵（matrix）。这个矩阵的每一行都代表一个源代码位置，并包含了对应的机器指令地址。

```shell
机器指令的地址   行, 列         源代码中新语句的开始    文件
0x0003386c  [   64,  5]         NS                  uri: "/usr/include/c++/11/bits/exception.h"
0x00033878  [   64, 34]         NS
0x00033886  [   64, 36]         NS
```

`DW_AT_stmt_list` 的值是一个指向 `.debug_line` 节（section）开头的偏移量（offset）。这个偏移量定位了与该特定编译单元相关联的行号表程序（line number program）的头部
## 结构

### header

每个编译单元（compilation unit, 通常对应一个源文件）在 .debug_line 节中都有自己的行号表。每个行号表的开头是一个头部

#### file_names

file_names 是行号表程序头部信息的一部分。它是一个包含一系列文件条目（file entries）的列表。这个列表定义了与当前编译单元（Compilation Unit, CU）的行号信息相关的所有源文件。
关键点：
范围： 它不仅仅包含编译单元的主源文件（例如，main.c），还包括所有通过 #include 指令（或其他类似机制，如 C++ 的 import) 包含进来的头文件或源文件。
顺序： 表中的第一个条目（索引为1，因为索引通常从1开始）总是代表编译单元的主源文件。这个主源文件的名称也通常由编译单元 DIE 中的 DW_AT_name 属性指定。

条目结构： 每个文件条目包含以下信息：
- 文件名（File Name）： 一个以 null 结尾的字符串，表示文件的名称。这可以是完整路径、相对路径或仅仅是文件名。
- 目录索引（Directory Index）： 一个无符号 LEB128 编码的整数。这个索引指向行号表头部中另一个列表——include_directories（包含目录表）中的一个条目。
如果目录索引为0，表示文件名本身已经是完整路径，或者该文件位于编译单元的编译目录（由 DW_AT_comp_dir 指定）。
如果目录索引大于0，则表示该文件位于 include_directories 列表中由该索引指定的目录中。
- 最后修改时间（Last Modification Time）： 一个无符号 LEB128 编码的整数，表示文件的最后修改时间戳（通常是自 Unix epoch 以来的秒数）。如果为0，表示该信息不可用。
- 文件大小（File Length/Size）： 一个无符号 LEB128 编码的整数，表示文件的字节大小。如果为0，表示该信息不可用。

### 状态机

行号表的抽象机包含用于存储行号表单行的存储空间。例如，它包含程序计数器值、源代码行号和列号以及源文件的字段。规范将这些字段称为寄存器，以类比 CPU 寄存器。行号表程序的指令会操纵这些寄存器，例如可能增加当前源代码行号或更改条目涉及的源文件。

#### 指令
- 标准操作码
- 扩展操作码,以0x00开始，其后跟着一个 ULEB128 值，给出下一条指令的长度。此长度之后是一个 uint8_t，提供实际的扩展操作码，然后是操作数
- 特殊操作码，用于在单个操作码中同时推进当前行号和地址，并发出一个矩阵行。特殊操作码由单个 uint8_t 组成，没有操作数。

