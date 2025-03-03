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