#include "Cifa.h"
#include <fstream>
#include <iostream>

using namespace cifa;

//Object sin(ObjectVector& d) { return sin(d[0]); }
//Object cos(ObjectVector& d) { return cos(d[0]); }
//Object pow(ObjectVector& d) { return pow(d[0].value, d[1].value); }

double test()
{
#include "1.c"
    ;
    return nan("");
}

int main()
{
    Cifa c1;
    //c1.register_function("sin", &sin);
    //c1.register_function("cos", &cos);
    std::ifstream ifs;
    ifs.open("1.c");
    std::string str;
    getline(ifs, str, '\0');
    auto o = c1.run_script(str);
    std::cout << "Cifa value is: " << o.value << "\n";
    std::cout << "C value is: " << test() << "\n";
}
