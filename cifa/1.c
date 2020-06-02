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
} sum = 0;
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
        continue;
        sum += (i * j) * x;
    }
}
auto a={
    5, 3
};
//sum.print();
return sum;
