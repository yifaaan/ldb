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
