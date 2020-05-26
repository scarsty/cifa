//cifa

//str = '';
int sum = 1;
if (sum == 1)
{
    if (sum > 0)
        sum = 100;
    else
        sum = 9;
}
else
{
    sum = 2;
}
for (int i = 1; i >= -10; i--)
{
    for (int j = 1; j <= 10; j++) { sum += (i * j) + (-1); }
}


return sum;
