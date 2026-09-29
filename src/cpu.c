#include "cpu.h"
#include <stdio.h>

enum { Z=0x80,N=0x40,H=0x20,CY=0x10 };
static uint16_t get_hl(CPU*c){return (uint16_t)((c->h<<8)|c->l);} static void set_hl(CPU*c,uint16_t v){c->h=v>>8;c->l=v;}
static uint16_t get_bc(CPU*c){return (uint16_t)((c->b<<8)|c->c);} static void set_bc(CPU*c,uint16_t v){c->b=v>>8;c->c=v;}
static uint16_t get_de(CPU*c){return (uint16_t)((c->d<<8)|c->e);} static void set_de(CPU*c,uint16_t v){c->d=v>>8;c->e=v;}
static uint16_t get_af(CPU*c){return (uint16_t)((c->a<<8)|c->f);} static void set_af(CPU*c,uint16_t v){c->a=v>>8;c->f=v&0xF0;}
static uint8_t fetch(CPU*c,Memory*m){if(c->halt_bug){c->halt_bug=0;return memory_read(m,c->pc);}return memory_read(m,c->pc++);} static uint16_t fetch16(CPU*c,Memory*m){uint8_t lo=fetch(c,m);return (uint16_t)(lo|((uint16_t)fetch(c,m)<<8));}
static void push(CPU*c,Memory*m,uint16_t v){memory_write(m,--c->sp,(uint8_t)(v>>8));memory_write(m,--c->sp,(uint8_t)v);} static uint16_t pop(CPU*c,Memory*m){uint8_t lo=memory_read(m,c->sp++),hi=memory_read(m,c->sp++);return (uint16_t)(lo|((uint16_t)hi<<8));}
static uint8_t rg(CPU*c,Memory*m,unsigned r){switch(r){case 0:return c->b;case 1:return c->c;case 2:return c->d;case 3:return c->e;case 4:return c->h;case 5:return c->l;case 6:return memory_read(m,get_hl(c));default:return c->a;}}
static void rs(CPU*c,Memory*m,unsigned r,uint8_t v){switch(r){case 0:c->b=v;break;case 1:c->c=v;break;case 2:c->d=v;break;case 3:c->e=v;break;case 4:c->h=v;break;case 5:c->l=v;break;case 6:memory_write(m,get_hl(c),v);break;default:c->a=v;}}
static uint16_t rp(CPU*c,unsigned p){switch(p){case 0:return get_bc(c);case 1:return get_de(c);case 2:return get_hl(c);default:return c->sp;}}
static void rps(CPU*c,unsigned p,uint16_t v){switch(p){case 0:set_bc(c,v);break;case 1:set_de(c,v);break;case 2:set_hl(c,v);break;default:c->sp=v;}}
static uint16_t rp2(CPU*c,unsigned p){return p==3?get_af(c):rp(c,p);}
static void rp2s(CPU*c,unsigned p,uint16_t v){if(p==3)set_af(c,v);else rps(c,p,v);}
static int cond(CPU*c,unsigned y){switch(y){case 0:return !(c->f&Z);case 1:return !!(c->f&Z);case 2:return !(c->f&CY);default:return !!(c->f&CY);}}
static void alu(CPU*c,unsigned op,uint8_t v){unsigned a=c->a,r;switch(op){case 0:r=a+v;c->f=(uint8_t)(((uint8_t)r?0:Z)|(((a&15)+(v&15)>15)?H:0)|(r>255?CY:0));c->a=r;break;case 1:{unsigned carry=!!(c->f&CY);r=a+v+carry;c->f=(uint8_t)(((uint8_t)r?0:Z)|(((a&15)+(v&15)+carry>15)?H:0)|(r>255?CY:0));c->a=r;break;}case 2:r=a-v;c->f=(uint8_t)(N|((uint8_t)r?0:Z)|((a&15)<(v&15)?H:0)|(a<v?CY:0));c->a=r;break;case 3:{unsigned carry=!!(c->f&CY);r=a-v-carry;c->f=(uint8_t)(N|((uint8_t)r?0:Z)|((a&15)<((v&15)+carry)?H:0)|(a<(unsigned)v+carry?CY:0));c->a=r;break;}case 4:c->a&=v;c->f=(uint8_t)((c->a?0:Z)|H);break;case 5:c->a^=v;c->f=c->a?0:Z;break;case 6:c->a|=v;c->f=c->a?0:Z;break;default:r=a-v;c->f=(uint8_t)(N|((uint8_t)r?0:Z)|((a&15)<(v&15)?H:0)|(a<v?CY:0));}}
static uint8_t inc8(CPU*c,uint8_t v){uint8_t r=v+1;c->f=(uint8_t)((c->f&CY)|(r?0:Z)|((v&15)==15?H:0));return r;} static uint8_t dec8(CPU*c,uint8_t v){uint8_t r=v-1;c->f=(uint8_t)((c->f&CY)|N|(r?0:Z)|((v&15)==0?H:0));return r;}
static void add_hl(CPU*c,uint16_t v){uint32_t a=get_hl(c),r=a+v;c->f=(uint8_t)((c->f&Z)|(((a&0xFFF)+(v&0xFFF)>0xFFF)?H:0)|(r>0xFFFF?CY:0));set_hl(c,r);}
static unsigned cb(CPU*c,Memory*m){uint8_t op=fetch(c,m),v=rg(c,m,op&7),r=v;unsigned x=op>>6,y=(op>>3)&7; if(x==0){switch(y){case 0:c->f=(uint8_t)((v&0x80?CY:0)|((uint8_t)(r=(uint8_t)((v<<1)|(v>>7)))?0:Z));break;case 1:c->f=(uint8_t)((v&1?CY:0)|((uint8_t)(r=(uint8_t)((v>>1)|(v<<7)))?0:Z));break;case 2:{unsigned k=!!(c->f&CY);c->f=(uint8_t)((v&0x80?CY:0)|((uint8_t)(r=(uint8_t)((v<<1)|k))?0:Z));break;}case 3:{unsigned k=!!(c->f&CY);c->f=(uint8_t)((v&1?CY:0)|((uint8_t)(r=(uint8_t)((v>>1)|(k<<7)))?0:Z));break;}case 4:c->f=(uint8_t)((v&0x80?CY:0)|((uint8_t)(r=v<<1)?0:Z));break;case 5:c->f=(uint8_t)((v&1?CY:0)|((uint8_t)(r=(uint8_t)((v>>1)|(v&0x80)))?0:Z));break;case 6:r=(uint8_t)((v<<4)|(v>>4));c->f=r?0:Z;break;default:c->f=(uint8_t)((v&1?CY:0)|((uint8_t)(r=v>>1)?0:Z));}rs(c,m,op&7,r);}else if(x==1)c->f=(uint8_t)((c->f&CY)|H|((v&(1u<<y))?0:Z));else if(x==2)rs(c,m,op&7,(uint8_t)(v&~(1u<<y)));else rs(c,m,op&7,(uint8_t)(v|(1u<<y)));return (op&7)==6?(x==1?12:16):8;}
void cpu_init(CPU*c,int skip){*c=(CPU){0};c->sp=0xFFFE;if(skip){c->a=1;c->f=0xB0;c->b=0;c->c=0x13;c->d=0;c->e=0xD8;c->h=1;c->l=0x4D;c->sp=0xFFFE;c->pc=0x100;}}
unsigned cpu_step(CPU*c,Memory*m){uint8_t ie=m->data[0xFFFF],pending=(uint8_t)(ie&m->data[0xFF0F]&0x1F);if(c->ime&&pending){for(unsigned i=0;i<5;i++)if(pending&(1u<<i)){c->ime=0;m->data[0xFF0F]&=(uint8_t)~(1u<<i);push(c,m,c->pc);c->pc=(uint16_t)(0x40+8*i);c->halted=0;c->cycles+=20;memory_tick(m,20);return 20;}}if(c->halted||c->stopped){if(pending)c->halted=c->stopped=0;else{c->cycles+=4;memory_tick(m,4);return 4;}}
uint8_t op=fetch(c,m);unsigned x=op>>6,y=(op>>3)&7,z=op&7,p=y>>1,q=y&1;unsigned cyc=4;c->instructions++;
if(op==0xCB){cyc=cb(c,m);goto done;}
if(x==1){if(op==0x76){if(!c->ime&&pending)c->halt_bug=1;else c->halted=1;cyc=4;}else{rs(c,m,y,rg(c,m,z));cyc=(y==6||z==6)?8:4;}goto done;}
if(x==2){alu(c,y,rg(c,m,z));cyc=z==6?8:4;goto done;}
if(x==0){switch(z){case 0:switch(y){case 0:cyc=4;break;case 1:{uint16_t a=fetch16(c,m);memory_write(m,a,c->sp&255);memory_write(m,a+1,c->sp>>8);cyc=20;break;}case 2:fetch(c,m);c->stopped=1;cyc=4;break;case 3:{int8_t r=(int8_t)fetch(c,m);c->pc+=r;cyc=12;break;}default:{int8_t r=(int8_t)fetch(c,m);if(cond(c,y-4)){c->pc+=r;cyc=12;}else cyc=8;break;}}break;
case 1:if(!q){rps(c,p,fetch16(c,m));cyc=12;}else{add_hl(c,rp(c,p));cyc=8;}break;
case 2:{uint16_t a=p==0?get_bc(c):p==1?get_de(c):get_hl(c);if(!q)memory_write(m,a,c->a);else c->a=memory_read(m,a);if(p==2)set_hl(c,a+1);if(p==3)set_hl(c,a-1);cyc=8;break;}
case 3:{uint16_t v=rp(c,p);rps(c,p,q?v-1:v+1);cyc=8;break;}
case 4:rs(c,m,y,inc8(c,rg(c,m,y)));cyc=y==6?12:4;break;
case 5:rs(c,m,y,dec8(c,rg(c,m,y)));cyc=y==6?12:4;break;
case 6:rs(c,m,y,fetch(c,m));cyc=y==6?12:8;break;
case 7:switch(y){case 0:{uint8_t b=c->a>>7;c->a=(uint8_t)((c->a<<1)|b);c->f=b?CY:0;break;}case 1:{uint8_t b=c->a&1;c->a=(uint8_t)((c->a>>1)|(b<<7));c->f=b?CY:0;break;}case 2:{uint8_t b=c->a>>7;c->a=(uint8_t)((c->a<<1)|!!(c->f&CY));c->f=b?CY:0;break;}case 3:{uint8_t b=c->a&1;c->a=(uint8_t)((c->a>>1)|((c->f&CY)?0x80:0));c->f=b?CY:0;break;}case 4:{uint8_t v=c->a,k=0;if(!(c->f&N)){if((c->f&H)||(v&15)>9)k|=6;if((c->f&CY)||v>0x99)k|=0x60,c->f|=CY;c->a+=k;}else{if(c->f&H)k|=6;if(c->f&CY)k|=0x60;c->a-=k;}c->f=(uint8_t)((c->f&N)|(c->a?0:Z)|(c->f&CY));break;}case 5:c->a=~c->a;c->f|=N|H;break;case 6:c->f=(uint8_t)((c->f&Z)|CY);break;case 7:c->f=(uint8_t)((c->f&Z)|((c->f&CY)?0:CY));break;}break;}goto done;}
if(x==3){switch(z){case 0:switch(y){case 0:case 1:case 2:case 3:case 4:case 5:case 6:case 7:if(y<4){if(cond(c,y)){c->pc=pop(c,m);cyc=20;}else cyc=8;}else if(y==4){memory_write(m,(uint16_t)(0xFF00|fetch(c,m)),c->a);cyc=12;}else if(y==5){int8_t e=(int8_t)fetch(c,m);uint16_t sp=c->sp;uint16_t u=(uint8_t)e;c->f=(uint8_t)((((sp&15)+(u&15)>15)?H:0)|(((sp&255)+u>255)?CY:0));c->sp=(uint16_t)(sp+e);cyc=16;}else if(y==6){c->a=memory_read(m,0xFF00|fetch(c,m));cyc=12;}else{int8_t e=(int8_t)fetch(c,m);uint16_t sp=c->sp;uint16_t u=(uint8_t)e;c->f=(uint8_t)((((sp&15)+(u&15)>15)?H:0)|(((sp&255)+u>255)?CY:0));set_hl(c,(uint16_t)(sp+e));cyc=12;}break;}break;
case 1:if(!q){rp2s(c,p,pop(c,m));cyc=12;}else if(p==0){c->pc=pop(c,m);cyc=16;}else if(p==1){c->pc=pop(c,m);c->ime=1;cyc=16;}else if(p==2){c->pc=get_hl(c);cyc=4;}else{c->sp=get_hl(c);cyc=8;}break;
case 2:if(y<4){uint16_t a=fetch16(c,m);if(cond(c,y)){c->pc=a;cyc=16;}else cyc=12;}else if(y==4){memory_write(m,0xFF00|c->c,c->a);cyc=8;}else if(y==5){memory_write(m,fetch16(c,m),c->a);cyc=16;}else if(y==6){c->a=memory_read(m,0xFF00|c->c);cyc=8;}else{c->a=memory_read(m,fetch16(c,m));cyc=16;}break;
case 3:if(y==0){c->pc=fetch16(c,m);cyc=16;}else if(y==1){cyc=cb(c,m);}else if(y==6){c->ime=0;c->ime_delay=0;cyc=4;}else if(y==7){c->ime_delay=2;cyc=4;}else {fprintf(stderr,"Illegal opcode %02X at %04X\n",op,(unsigned)(c->pc-1));cyc=4;}break;
case 4:if(y<4){uint16_t a=fetch16(c,m);if(cond(c,y)){push(c,m,c->pc);c->pc=a;cyc=24;}else cyc=12;}else{fprintf(stderr,"Illegal opcode %02X at %04X\n",op,(unsigned)(c->pc-1));cyc=4;}break;
case 5:if(!q){push(c,m,rp2(c,p));cyc=16;}else if(p==0){uint16_t a=fetch16(c,m);push(c,m,c->pc);c->pc=a;cyc=24;}else{fprintf(stderr,"Illegal opcode %02X at %04X\n",op,(unsigned)(c->pc-1));cyc=4;}break;
case 6:alu(c,y,fetch(c,m));cyc=8;break;
case 7:push(c,m,c->pc);c->pc=(uint16_t)(y*8);cyc=16;break;}}
done:c->f&=0xF0;if(c->ime_delay&&--c->ime_delay==0)c->ime=1;c->cycles+=cyc;memory_tick(m,cyc);return cyc;}
