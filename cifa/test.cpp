#include "../../common/convert.h"
#include "Cifa.h"
#include <cmath>
#include <iostream>

using namespace cifa;

Object sin(ObjectVector& d) { return sin(d[0]); }
Object cos(ObjectVector& d) { return cos(d[0]); }
Object pow(ObjectVector& d) { return pow(d[0].value, d[1].value); }

int main()
{
    Cifa c1;
    c1.register_function("sin", &sin);
    c1.register_function("cos", &cos);
    std::string str = convert::readStringFromFile("1.c");
    c1.run_script(str);
}
