#include <iostream>
#include <string>

void PrintType(int) {
  int a = 1;
  std::cout << "int";
}

void PrintType(double) {
  int a = 1;
  std::cout << "double";
}

void PrintType(std::string) {
  int a = 1;
  std::cout << "string";
}

int main() {
  PrintType(0);
  PrintType(1.4);
  PrintType("Hello");
}