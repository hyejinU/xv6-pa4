#include "param.h" 
#include "types.h" 
#include "stat.h" 
#include "user.h" 
#include "fs.h" 
#include "fcntl.h" 
#include "syscall.h" 
#include "traps.h" 
#include "memlayout.h" 
 
#define LOOP 800 
 
char* arr[LOOP]; 

int main () { 
    sbrk(4096*670); 
    for (int i =0 ; i<LOOP;i++){ 
        if(i%10 == 0) 
            printf(1,"proc sbrk %d\n",i); 
        char* p = sbrk(4096); 
        if(p==(char*)-1) break; 
        *p = 'A'; 
        arr[i]=p; 
    } 
    printf(1,"finish sbrk\n"); 
    for(int i=0;i<LOOP;i+=200){ 
        printf(1,"print %d : %x ->%c\n",i,(int)arr[i],*arr[i]); 
    } 
    int a,b; 
    swapstat(&a,&b); 
    printf(1,"swapstat %d %d\n",a,b); 
    exit(); 
}
