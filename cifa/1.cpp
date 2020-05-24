#include "../../common/convert.h"
#include "Cifa.h"
#include <cmath>
#include <iostream>

using namespace cifa;

Object sin(ObjectVector& d) { return sin(d[0]); }
Object cos(ObjectVector& d) { return cos(d[0]); }
Object pow(ObjectVector& d) { return pow(d[0], d[1]); }

int main()
{
    Cifa c1;
    c1.register_function("sin", &sin);
    c1.register_function("cos", &cos);
    c1.register_function("pow", &pow);

    std::string str = " if (1>0) {x=-(!355/113)+4-5,y=0.5;x;pow(-cos(x),y);x=x-1;}";
    //auto strs = convert::splitString(str, ";");
    //for (auto s : strs)
    //{
    //    std::cout << s << " ...... " << c1.run_line(s) << '\n';
    //}
    c1.run_line(str);
}
