//cifa

//str = '';
int sum = 1;
if (sum == 1)
{
    if (sum > 0)
        sum = 100;
    else
        sum = 9;
    ;
}
else
{
    sum = 2;
};
for (int i = 1; i <= 10; i++)
{
    if (i == 5) continue;

    for (int j = 1; j <= 10; j++)
    {
        if (j == 7) break;
        int x = 1;
        if ((i + j) % 2 == 0)
        { x = -1; }
        sum += (i * j) * x;
    }
}
break;
return sum;
