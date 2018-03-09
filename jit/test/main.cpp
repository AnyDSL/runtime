#include <iostream>
#include <cstring>

#include "jit.h"

int main(int argc, char** argv) {
    const char* str = "extern fn get_value() -> int { 42 }";
    if (auto ptr = anydsl_compile(str, strlen(str), "get_value", 3)) {
        auto fn = reinterpret_cast<int (*)()>(ptr);
        std::cout << "value: " << fn() << std::endl;
        return 0;
    }
    return 1;
}
