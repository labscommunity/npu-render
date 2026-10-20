typedef unsigned uint4 __attribute__((ext_vector_type(4),may_alias));
__attribute__((noinline)) void *nr_vector_memset(void *destination,int value,__SIZE_TYPE__ count){
 volatile unsigned char *p=(volatile unsigned char*)destination;const unsigned char byte=(unsigned char)value;
 while(count&&((__UINTPTR_TYPE__)p&15)){*p++=byte;--count;}
 const uint4 fill=(uint4)((unsigned)byte*0x01010101u);
 while(count>=16){*(volatile uint4*)p=fill;p+=16;count-=16;}
 while(count){*p++=byte;--count;}
 return destination;
}
