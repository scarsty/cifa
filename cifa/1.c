//cifa

str = '';
for (int i = 1; i <= 100; i += 1)
{
    if (i == 50) continue;
    sum += i;
    str += to_string(i);
    if (i == 90) break;
    if (i < 100) str += "+";
}
print(str, "=", sum);
//ccc(;