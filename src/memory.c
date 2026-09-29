#include "memory.h"
#include <stdio.h>
#include <string.h>

#define GB_CLOCK_HZ 4194304u
#define GB_AUDIO_RATE 44100u
static void put_le16(FILE*f,uint16_t v){fputc(v&255,f);fputc(v>>8,f);}
static void put_le32(FILE*f,uint32_t v){put_le16(f,(uint16_t)v);put_le16(f,(uint16_t)(v>>16));}
static void wav_header(FILE*f,uint32_t bytes){rewind(f);fwrite("RIFF",1,4,f);put_le32(f,36+bytes);fwrite("WAVEfmt ",1,8,f);put_le32(f,16);put_le16(f,1);put_le16(f,2);put_le32(f,GB_AUDIO_RATE);put_le32(f,GB_AUDIO_RATE*4);put_le16(f,4);put_le16(f,16);fwrite("data",1,4,f);put_le32(f,bytes);}
int memory_audio_open(Memory*m,const char*path){FILE*f=fopen(path,"wb+");if(!f){perror(path);return 0;}m->audio_file=f;m->audio_samples=0;m->audio_sample_phase=0;m->audio_elapsed_cycles=0;m->audio_frame_cycles=0;m->noise_lfsr=0x7FFF;wav_header(f,0);return 1;}
int memory_audio_close(Memory*m){if(!m->audio_file)return 1;FILE*f=(FILE*)m->audio_file;uint32_t bytes=m->audio_samples*4u;wav_header(f,bytes);int ok=fflush(f)==0;ok=(fclose(f)==0)&&ok;m->audio_file=NULL;return ok;}
static void audio_status(Memory*m){m->data[0xFF26]=(uint8_t)(0x70|(m->data[0xFF26]&0x80)|(m->audio_active&15));}
static int audio_dac(const Memory*m,unsigned ch){static const unsigned regs[]={0xFF12,0xFF17,0,0xFF21};return ch==2?!!(m->data[0xFF1A]&0x80):!!(m->data[regs[ch]]&0xF8);}
static void audio_trigger(Memory*m,unsigned ch){if(audio_dac(m,ch)){m->audio_active|=(uint8_t)(1u<<ch);if(!m->audio_length[ch])m->audio_length[ch]=ch==2?256:64;if(ch<3){unsigned r=ch==0?0xFF12:ch==1?0xFF17:0; m->audio_volume[ch]=m->data[r]>>4;m->audio_envelope_cycles[ch]=0;m->audio_envelope_timer[ch]=(m->data[r]&7)?(m->data[r]&7):8;}else m->noise_lfsr=0x7FFF;}else m->audio_active&=(uint8_t)~(1u<<ch);audio_status(m);}
static int channel_sample(Memory*m,unsigned ch,uint32_t elapsed){
    static const uint8_t duty[4]={0x01,0x81,0x87,0x7E};
    if(!(m->audio_active&(1u<<ch))||!audio_dac(m,ch))return 0;
    if(ch<2){unsigned lo=ch?0xFF18:0xFF13,hi=ch?0xFF19:0xFF14,ctrl=ch?0xFF16:0xFF11;
        unsigned frequency=m->data[lo]|((m->data[hi]&7)<<8);unsigned period=(2048-frequency)*4;if(!period)period=4;
        m->audio_phase[ch]=(m->audio_phase[ch]+elapsed)%(period*8u);unsigned pos=m->audio_phase[ch]/period;unsigned pattern=duty[m->data[ctrl]>>6];int wave=(pattern>>(7-pos))&1;
        unsigned env=ch?0xFF17:0xFF12;unsigned pace=m->data[env]&7;
        if(pace){m->audio_envelope_cycles[ch]+=elapsed;while(m->audio_envelope_cycles[ch]>=pace*65536u){m->audio_envelope_cycles[ch]-=pace*65536u;if(m->data[env]&8){if(m->audio_volume[ch]<15)m->audio_volume[ch]++;}else if(m->audio_volume[ch])m->audio_volume[ch]--;}}
        return wave?(int)m->audio_volume[ch]*2-15:-15;
    }
    if(ch==2){unsigned frequency=m->data[0xFF1D]|((m->data[0xFF1E]&7)<<8);unsigned period=(2048-frequency)*2;if(!period)period=2;
        m->audio_phase[2]=(m->audio_phase[2]+elapsed)%(period*32u);unsigned sample=m->audio_phase[2]/period;uint8_t packed=m->data[0xFF30+sample/2];int value=(sample&1)?packed&15:packed>>4;unsigned level=(m->data[0xFF1C]>>5)&3;if(level==0)return 0;if(level==2)value>>=1;else if(level==3)value>>=2;return value*2-15;}
    unsigned divisor=m->data[0xFF22]&7;unsigned period=(divisor?divisor*16:8)<<((m->data[0xFF22]>>4)&15);if(period<8)period=8;m->noise_cycles+=elapsed;
    while(m->noise_cycles>=period){m->noise_cycles-=period;unsigned bit=(m->noise_lfsr^(m->noise_lfsr>>1))&1;m->noise_lfsr=(uint16_t)((m->noise_lfsr>>1)|(bit<<14));if(m->data[0xFF22]&8)m->noise_lfsr=(uint16_t)((m->noise_lfsr&~0x40)|(bit<<6));}
    unsigned envreg=0xFF21,pace=m->data[envreg]&7;if(pace){m->audio_envelope_cycles[2]+=elapsed;while(m->audio_envelope_cycles[2]>=pace*65536u){m->audio_envelope_cycles[2]-=pace*65536u;if(m->data[envreg]&8){if(m->audio_volume[2]<15)m->audio_volume[2]++;}else if(m->audio_volume[2])m->audio_volume[2]--;}}
    return (m->noise_lfsr&1)?-(int)m->audio_volume[2]:(int)m->audio_volume[2];
}
static void audio_emit(Memory*m,uint32_t elapsed){FILE*f=(FILE*)m->audio_file;if(!f)return;unsigned nr50=m->data[0xFF24],nr51=m->data[0xFF25];int channel[4];for(unsigned i=0;i<4;i++)channel[i]=channel_sample(m,i,elapsed);
    int left=0,right=0;for(unsigned i=0;i<4;i++){if(nr51&(1u<<i))right+=channel[i];if(nr51&(1u<<(i+4)))left+=channel[i];}
    left=left*(int)(((nr50>>4)&7)+1)*40;right=right*(int)((nr50&7)+1)*40;
    int16_t l=(int16_t)left,r=(int16_t)right;put_le16(f,(uint16_t)l);put_le16(f,(uint16_t)r);m->audio_samples++;
}
static void apu_tick(Memory*m,unsigned cycles){
    if(m->audio_file){m->audio_elapsed_cycles+=cycles;m->audio_sample_phase+=cycles*GB_AUDIO_RATE;
        while(m->audio_sample_phase>=GB_CLOCK_HZ){m->audio_sample_phase-=GB_CLOCK_HZ;uint32_t elapsed=m->audio_elapsed_cycles;m->audio_elapsed_cycles=0;audio_emit(m,elapsed);}}
    m->audio_frame_cycles+=cycles;while(m->audio_frame_cycles>=16384){m->audio_frame_cycles-=16384;for(unsigned i=0;i<4;i++)if(m->audio_length[i]&&(m->data[i==0?0xFF14:i==1?0xFF19:i==2?0xFF1E:0xFF23]&0x40)){if(--m->audio_length[i]==0)m->audio_active&=(uint8_t)~(1u<<i);}audio_status(m);}}

static unsigned bank_count(uint8_t code) {
    static const unsigned counts[] = {2,4,8,16,32,64,128,256,512};
    if (code <= 8) return counts[code];
    if (code == 0x52) return 72;
    if (code == 0x53) return 80;
    if (code == 0x54) return 96;
    return 2;
}
void memory_init(Memory *m) {
    memset(m, 0, sizeof(*m)); m->joypad=0xFF; m->rom_bank=1;
    m->data[0xFF00]=0xCF; m->data[0xFF0F]=0xE1; m->data[0xFF41]=0x80; m->data[0xFFFF]=0;
}
int memory_load_rom(Memory *m, const char *path, char *error, size_t n) {
    FILE *f=fopen(path,"rb"); if(!f){snprintf(error,n,"cannot open ROM '%s'",path);return 0;}
    size_t got=fread(m->rom,1,sizeof m->rom,f); int extra=fgetc(f); int read_error=ferror(f); fclose(f);
    if(read_error){snprintf(error,n,"error reading ROM");return 0;}
    if(extra!=EOF){snprintf(error,n,"ROM exceeds supported 2 MiB size");return 0;}
    if(got<0x150){snprintf(error,n,"ROM is too small to contain a cartridge header");return 0;}
    m->rom_size=got; m->cartridge_type=m->rom[0x147];
    m->rom_banks=bank_count(m->rom[0x148]); if(m->rom_banks>got/0x4000) m->rom_banks=(unsigned)(got/0x4000);
    if(!m->rom_banks)m->rom_banks=1;
    m->ram_banks=m->rom[0x149]==1?1:m->rom[0x149]==2?1:m->rom[0x149]==3?4:0;
    m->rom_bank=1; return 1;
}
void memory_request_interrupt(Memory *m,uint8_t bit){m->data[0xFF0F]|=(uint8_t)(1u<<bit);}
static unsigned ppu_mode(const Memory *m){return m->data[0xFF41]&3;}
static void stat_refresh(Memory *m){
    uint8_t stat=m->data[0xFF41];unsigned mode=ppu_mode(m);int coincidence=m->data[0xFF44]==m->data[0xFF45];
    stat=(uint8_t)((stat&~7u)|(coincidence?4:0)|mode);m->data[0xFF41]=stat;
    int line=((mode==0&&(stat&8))||(mode==1&&(stat&0x10))||(mode==2&&(stat&0x20))||(coincidence&&(stat&0x40)));
    if(line&&!m->stat_irq_line)memory_request_interrupt(m,1);
    m->stat_irq_line=(uint8_t)line;
}
static uint8_t tile_pixel(Memory*m,unsigned map,unsigned px,unsigned py){
    uint8_t tile=m->data[map+(py>>3)*32+(px>>3)];int base=(m->data[0xFF40]&0x10)?0x8000:0x9000;
    int ti=(m->data[0xFF40]&0x10)?tile:(int8_t)tile;unsigned a=(unsigned)(base+ti*16+(py&7)*2);unsigned bit=7-(px&7);
    return (uint8_t)((((m->data[a+1]>>bit)&1)<<1)|((m->data[a]>>bit)&1));
}
static void render_scanline(Memory*m,unsigned y){
    uint8_t lcdc=m->data[0xFF40];if(!(lcdc&0x80)||y>=144)return;unsigned bg_map=(lcdc&8)?0x9C00:0x9800;
    int window_used=0;uint8_t bgraw[160];
    for(unsigned x=0;x<160;x++){
        uint8_t raw=0;
        if(lcdc&1){unsigned wx=m->data[0xFF4B];int use_window=(lcdc&0x20)&&y>=m->data[0xFF4A]&&wx<=166&&x+7>=wx;
            if(use_window){unsigned map=(lcdc&0x40)?0x9C00:0x9800;raw=tile_pixel(m,map,x+7-wx,m->window_line);window_used=1;}
            else raw=tile_pixel(m,bg_map,(x+m->data[0xFF43])&255,(y+m->data[0xFF42])&255);
        }
        bgraw[x]=raw;m->framebuffer[y*160+x]=(uint8_t)((m->data[0xFF47]>>(raw*2))&3);
    }
    if(window_used)m->window_line++;
    if(!(lcdc&2))return;
    int height=(lcdc&4)?16:8;unsigned selected[10],count=0;
    for(unsigned i=0;i<40&&count<10;i++){int sy=(int)m->data[0xFE00+i*4]-16;if((int)y>=sy&&(int)y<sy+height)selected[count++]=i;}
    for(unsigned x=0;x<160;x++){
        int best_x=1000;unsigned best_i=40;uint8_t best_color=0,best_attr=0;
        for(unsigned k=0;k<count;k++){unsigned i=selected[k];int sx=(int)m->data[0xFE01+i*4]-8;if((int)x<sx||(int)x>=sx+8)continue;
            uint8_t attr=m->data[0xFE03+i*4];int row=(int)y-((int)m->data[0xFE00+i*4]-16);int col=(int)x-sx;if(attr&0x40)row=height-1-row;if(attr&0x20)col=7-col;
            unsigned tile=m->data[0xFE02+i*4];if(height==16){tile&=0xFE;tile+=row/8;row&=7;}unsigned a=0x8000+tile*16+(unsigned)row*2;unsigned bit=7-(unsigned)col;uint8_t color=(uint8_t)((((m->data[a+1]>>bit)&1)<<1)|((m->data[a]>>bit)&1));
            if(!color)continue;if(sx<best_x||(sx==best_x&&i<best_i)){best_x=sx;best_i=i;best_color=color;best_attr=attr;}}
        if(best_i<40&&(!(best_attr&0x80)||bgraw[x]==0)){uint8_t pal=m->data[(best_attr&0x10)?0xFF49:0xFF48];m->framebuffer[y*160+x]=(uint8_t)((pal>>(best_color*2))&3);}
    }
}
static void ppu_tick(Memory*m,unsigned cycles){
    if(!(m->data[0xFF40]&0x80))return;
    while(cycles--){m->ppu_line_cycles++;unsigned ly=m->data[0xFF44];
        if(ly<144){if(m->ppu_line_cycles==80){m->data[0xFF41]=(uint8_t)((m->data[0xFF41]&~3u)|3);stat_refresh(m);}else if(m->ppu_line_cycles==252){render_scanline(m,ly);m->data[0xFF41]=(uint8_t)(m->data[0xFF41]&~3u);stat_refresh(m);}}
        if(m->ppu_line_cycles>=456){m->ppu_line_cycles=0;ly++;if(ly==144){m->data[0xFF44]=(uint8_t)ly;m->data[0xFF41]=(uint8_t)((m->data[0xFF41]&~3u)|1);m->ppu_frames++;memory_request_interrupt(m,0);stat_refresh(m);}else if(ly>153){m->data[0xFF44]=0;m->window_line=0;m->data[0xFF41]=(uint8_t)((m->data[0xFF41]&~3u)|2);stat_refresh(m);}else{m->data[0xFF44]=(uint8_t)ly;if(ly<144)m->data[0xFF41]=(uint8_t)((m->data[0xFF41]&~3u)|2);stat_refresh(m);}}
    }
}
void memory_set_button(Memory*m,unsigned button,int pressed){if(button>7)return;uint8_t before=memory_read(m,0xFF00);if(pressed)m->joypad&=(uint8_t)~(1u<<button);else m->joypad|=(uint8_t)(1u<<button);uint8_t after=memory_read(m,0xFF00);if((before&~after&0x0F)!=0)memory_request_interrupt(m,4);}
uint8_t memory_read(Memory *m,uint16_t a) {
    if(a<0x4000 && m->rom_size) {
        unsigned b=(m->banking_mode && (m->cartridge_type==1||m->cartridge_type==2||m->cartridge_type==3)) ? ((m->rom_bank&0x60)%m->rom_banks):0;
        size_t i=(size_t)b*0x4000+a; return i<m->rom_size?m->rom[i]:0xFF;
    }
    if(a<0x8000 && m->rom_size) {unsigned b=m->rom_bank%m->rom_banks;size_t i=(size_t)b*0x4000+(a-0x4000);return i<m->rom_size?m->rom[i]:0xFF;}
    if(a>=0xA000&&a<0xC000&&m->rom_size&&(m->cartridge_type==1||m->cartridge_type==2||m->cartridge_type==3)) {
        if(!m->ram_enabled||!m->ram_banks)return 0xFF;
        unsigned b=m->ram_banks>1?m->ram_bank%m->ram_banks:0;return m->eram[b*0x2000+(a-0xA000)];
    }
    if(a>=0x8000&&a<0xA000&&(m->data[0xFF40]&0x80)&&ppu_mode(m)==3)return 0xFF;
    if(a>=0xFE00&&a<0xFEA0&&(m->data[0xFF40]&0x80)&&(ppu_mode(m)==2||ppu_mode(m)==3))return 0xFF;
    if(a>=0xE000&&a<0xFE00)a-=0x2000;
    if(a>=0xFEA0&&a<0xFF00)return 0xFF;
    if(a==0xFF00) {uint8_t select=m->data[a]&0x30;uint8_t low=0x0F;if(!(select&0x10))low&=m->joypad&0x0F;if(!(select&0x20))low&=(m->joypad>>4)&0x0F;return (uint8_t)(0xC0|select|low);}
    if(a==0xFF04)return (uint8_t)(m->div_counter>>8);
    if(a==0xFF0F)return (uint8_t)(m->data[a]|0xE0);
    if(a==0xFF26)return (uint8_t)(m->data[a]|0x70|(m->audio_active&15));
    return m->data[a];
}
void memory_write(Memory *m,uint16_t a,uint8_t v) {
    if(a<0x8000&&m->rom_size) {
        if(m->cartridge_type==1||m->cartridge_type==2||m->cartridge_type==3) {
            if(a<0x2000)m->ram_enabled=(v&0x0F)==0x0A;
            else if(a<0x4000){m->rom_bank=(m->rom_bank&0x60)|(v&0x1F);if(!(m->rom_bank&0x1F))m->rom_bank|=1;}
            else if(a<0x6000){if(m->banking_mode)m->ram_bank=v&3;else m->rom_bank=(m->rom_bank&0x1F)|((v&3)<<5);}
            else m->banking_mode=v&1;
        }return;
    }
    if(a>=0xA000&&a<0xC000&&m->rom_size&&(m->cartridge_type==1||m->cartridge_type==2||m->cartridge_type==3)) {
        if(m->ram_enabled&&m->ram_banks){unsigned b=m->ram_banks>1?m->ram_bank%m->ram_banks:0;m->eram[b*0x2000+(a-0xA000)]=v;}return;
    }
    if(a>=0x8000&&a<0xA000&&(m->data[0xFF40]&0x80)&&ppu_mode(m)==3)return;
    if(a>=0xFE00&&a<0xFEA0&&(m->data[0xFF40]&0x80)&&(ppu_mode(m)==2||ppu_mode(m)==3))return;
    if(a>=0xE000&&a<0xFE00){m->data[a]=v;m->data[a-0x2000]=v;return;}
    if(a>=0xFEA0&&a<0xFF00)return;
    if(a==0xFF04){m->div_counter=0;m->data[a]=0;return;}
    if(a==0xFF07){uint8_t old=memory_read(m,0xFF07);m->data[a]=(uint8_t)(v|0xF8);static const unsigned bits[]={9,3,5,7};unsigned bit=bits[v&3];if((old&4)&&!(v&4)&&((m->div_counter>>bit)&1))m->data[0xFF05]++;return;}
    if(a==0xFF40){uint8_t old=m->data[a];m->data[a]=v;if((old&0x80)&&!(v&0x80)){m->data[0xFF44]=0;m->ppu_line_cycles=0;m->window_line=0;m->data[0xFF41]&=0xFC;m->stat_irq_line=0;memset(m->framebuffer,0,sizeof m->framebuffer);}else if(!(old&0x80)&&(v&0x80)){m->data[0xFF44]=0;m->ppu_line_cycles=0;m->window_line=0;m->data[0xFF41]=(uint8_t)((m->data[0xFF41]&~3u)|2);stat_refresh(m);}return;}
    if(a==0xFF41){m->data[a]=(uint8_t)((m->data[a]&7)|(v&0x78)|0x80);stat_refresh(m);return;}
    if(a==0xFF44){m->data[a]=0;m->ppu_line_cycles=0;stat_refresh(m);return;}
    if(a==0xFF45){m->data[a]=v;stat_refresh(m);return;}
    if(a==0xFF46){m->data[a]=v;m->dma_active=1;uint16_t base=(uint16_t)(v<<8);for(unsigned i=0;i<0xA0;i++){uint8_t value=memory_read(m,(uint16_t)(base+i));m->data[0xFE00+i]=value;}m->dma_active=0;return;}
    if(a==0xFF05&&m->timer_reload_pending){m->timer_reload_pending=0;}
    if(a==0xFF26){if(!(v&0x80)){memset(&m->data[0xFF10],0,0x16);m->data[a]=0x70;m->audio_active=0;}else m->data[a]=(uint8_t)(0xF0|(m->audio_active&15));return;}
    if(a>=0xFF10&&a<=0xFF25){if(!(m->data[0xFF26]&0x80))return;m->data[a]=v;
        if(a==0xFF11)m->audio_length[0]=64-(v&0x3F);else if(a==0xFF16)m->audio_length[1]=64-(v&0x3F);else if(a==0xFF1B)m->audio_length[2]=256-v;else if(a==0xFF20)m->audio_length[3]=64-(v&0x3F);
        if(a==0xFF12&&!(v&0xF8))m->audio_active&=(uint8_t)~1u;
        if(a==0xFF17&&!(v&0xF8))m->audio_active&=(uint8_t)~2u;
        if(a==0xFF1A&&!(v&0x80))m->audio_active&=(uint8_t)~4u;
        if(a==0xFF21&&!(v&0xF8))m->audio_active&=(uint8_t)~8u;
        if(a==0xFF14&&(v&0x80))audio_trigger(m,0);
        if(a==0xFF19&&(v&0x80))audio_trigger(m,1);
        if(a==0xFF1E&&(v&0x80))audio_trigger(m,2);
        if(a==0xFF23&&(v&0x80))audio_trigger(m,3);
        audio_status(m);return;}
    if(a==0xFF06)m->data[a]=v;
    else if(a==0xFF0F)m->data[a]=(uint8_t)(v|0xE0);
    else if(a==0xFF00)m->data[a]=(uint8_t)((m->data[a]&0xCF)|(v&0x30));
    else m->data[a]=v;
}
void memory_tick(Memory *m,unsigned cycles) {
    static const unsigned bits[]={9,3,5,7};
    for(unsigned i=0;i<cycles;i++) {
        uint8_t tac=m->data[0xFF07]; unsigned bit=bits[tac&3]; int old=(m->div_counter>>bit)&1;
        m->div_counter++;m->data[0xFF04]=(uint8_t)(m->div_counter>>8);
        int now=(m->div_counter>>bit)&1;
        if((tac&4)&&old&&!now){if(m->data[0xFF05]==0xFF){m->data[0xFF05]=0;m->timer_reload_pending=1;}else m->data[0xFF05]++;}
        if(m->timer_reload_pending){m->data[0xFF05]=m->data[0xFF06];memory_request_interrupt(m,2);m->timer_reload_pending=0;}
    }
    apu_tick(m,cycles);
    ppu_tick(m,cycles);
}
