int f = 6;

int g(int* f){
    return (*f)++;
}

int main()
{
    printf(g(&f));
    printf(f);
}