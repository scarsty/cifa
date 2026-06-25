#include "../Cifa.h"
#include <cmath>

static double plugin_square(double x)
{
    return x * x;
}

static double plugin_add(double a, double b)
{
    return a + b;
}

extern "C" __declspec(dllexport) int cifa_import(cifa::Cifa* cifa)
{
    if (cifa == nullptr)
    {
        return 0;
    }

    cifa->register_function("plugin_square", plugin_square);
    cifa->register_function("plugin_add", plugin_add);
    cifa->register_function("plugin_sin", static_cast<double (*)(double)>(&std::sin));
    return 1;
}
