int main() {
  // 由于i是volatile的，编译器不会优化这个循环，从而导致CPU持续处于忙碌状态。
  volatile int i = 0;
  while (true) {
    i = 42;
  }
}