//cifa
x = 68;

if (x > 9)
{
    x = x - 1;
}
else
{
    x = 9;
}
sum = 0;
for (int i = 1; i <= 100; i = i + 1)
{
    sum += i;
    print(sum);
}
print(sum);
print(x);