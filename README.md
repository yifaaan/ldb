# LDB

LDB is a debugging tool for C++ programs.

## Notes

### Attaching to a Process

```bash
# Attach to a program
ldb <program name>
# Attach to an existing process
ldb -p <pid>
```

### Handling User Input
Same as `gdb` and `lldb`.

```bash
break set 0xcafecafe
continue
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