#include "cifa.h"
#include <iostream>
#include <cmath>

int main()
{
    register_function("sin", &sin);
    run("x = 5");
    std::string str = "  (1.6 + x) + (87  +90)* 6";
    std::cout << str << " = " <<run(str) << '\n';
}
