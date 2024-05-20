#include <stdio.h>
#define f (1<<14)

int int_to_fp(int n){
    return n * f;
}

int fp_to_int(int x){
    return x / f;
}

int fp_to_int_round(int x){
    if(x >= 0){
        return (x+f/2)/f;
    }else{
        return (x-f/2)/f;
    }
}

int add_fp(int x, int y){
    return x+y;
}

int sub_fp(int x, int y){
    return x-y;
}

int add_mixed(int x, int n){
    return x+n*f;
}

int sub_mixed(int x, int n){
    return x-n*f;
}

int multi_fp(int x, int y){
    return ((int64_t)x)*y/f;
}

int multi_mixed(int x, int n){
    return x * n;
}

int div_fp(int x, int y){
    return ((int64_t)x)*f/y;
}

int div_mixed(int x, int n){
    return x/n;

}