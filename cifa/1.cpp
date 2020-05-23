#include "../../common/convert.h"
#include "Cifa.h"
#include <cmath>
#include <iostream>


object sin(object_vector& d) { return sin(d[0]); }
object cos(object_vector& d) { return cos(d[0]); }
object pow(object_vector& d) { return pow(d[0], d[1]); }

int main()
{
    Cifa c1;
    c1.register_function("sin", &sin);
    c1.register_function("cos", &cos);
    c1.register_function("pow", &pow);

    std::string str = " x=-(113/355),y=0.5;x;pow(0-cos(x),y);x=x-1";
    auto strs = convert::splitString(str, ";");
    for (auto s : strs)
    {
        std::cout << s << " ...... " << c1.run_line(s) << '\n';
    }
}
