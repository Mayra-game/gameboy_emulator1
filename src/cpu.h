#ifndef GB_CPU_H
#define GB_CPU_H
#include "memory.h"
typedef struct { uint8_t a,f,b,c,d,e,h,l; uint16_t sp,pc; uint8_t ime,halted,stopped,halt_bug,ime_delay; uint64_t cycles,instructions; } CPU;
void cpu_init(CPU *c, int skip_boot);
unsigned cpu_step(CPU *c, Memory *m);
#endif
