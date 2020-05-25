//cifa

for (int i = 1; i <= 100; i = i + 1)
{
    if (i == 50) continue;
    sum += i;
}
str = 'sum=' + "1+...+100 - 50";
print(str, "=", sum);