// usage: dis <flatfile> <baseaddr hex> [startoff hex] [len hex]
#include <capstone/capstone.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  unsigned char*b=malloc(n); fread(b,1,n,f); fclose(f);
  unsigned long base=strtoul(argv[2],0,16);
  long so=argc>3?strtoul(argv[3],0,16):0; long len=argc>4?strtoul(argv[4],0,16):n-so;
  if(so+len>n)len=n-so;
  csh h; cs_open(CS_ARCH_X86,CS_MODE_32,&h);
  cs_insn*i; size_t pos=so, end=so+len;
  while(pos<end){
    size_t c=cs_disasm(h,b+pos,end-pos,base+pos,1,&i);
    if(!c){printf("%08lx: db 0x%02x\n",base+pos,b[pos]);pos++;continue;}
    printf("%08lx: ",(unsigned long)i->address);
    for(int k=0;k<i->size;k++)printf("%02x",i->bytes[k]);
    printf("\t%s %s\n",i->mnemonic,i->op_str);
    pos+=i->size; cs_free(i,1);
  }
}
