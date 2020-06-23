//cifa

int sum = -1;
if (sum == -1)
{
    if (sum > 0)
        ;
    else
        sum = 9;
    ;
}
else
{
    sum = 2;
}
int c = sum = (-sum);
for (int i = 1; i <= 10; i++)
{
    if (i == 5) continue;

    for (int j = 1; j <= 10; j++)
    {
        int x = 1;
        if (j == 7)
            break;
        else if ((i + j) % 2 == 0)
        {
            x = -1;
        }
        else
            x = 0;
        sum += (i * j) * x;
    }
}
/*c.print();
//auto pi = 3.1415927;
print(sin(degree*pi/180));
print(cos(pi / 6));
print(pow(2, 10));*/
return sum;
