
#include "ansi/w65c02.h"
#include "comm.h"
#include "state.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "nand.h"
#ifdef TAB5_PORT
#include "esp_heap_caps.h"
#endif
extern WqxRom nc2k_rom;
extern nc2k_states_t nc2k_states;
static uint8_t* ram_buff = nc2k_states.ram;
static uint8_t* ram_io = nc2k_states.ram_io;

static deque<uint8_t> nand_cmd;
static deque<uint8_t> nand_addr;
static deque<uint8_t> nand_data;

static int nand_read_cnt=0;

char nand_magic[11];

/* ── Tab5 NAND demand-paging ──────────────────────────────────────────────
 * The full NAND (nc2000: (64+65536)*528 ≈ 33 MB, nc3000: ~66 MB) does not fit
 * in the 32 MB PSRAM, so instead of the original
 *     static char nand[65536*2+64][528];   // ~66 MB
 * we keep only the 33 KB boot block (pages 0..63 = the ".nand0" file) resident
 * and demand-page the large main array (the ".nand" file on SD) one erase block
 * (32 pages = 16896 B) at a time through a small write-back LRU cache. Every
 * access in this file is byte-offset based (`char *p=&nand[0][0]; p[final]`),
 * so we route those bytes through nb_get / nb_set / nb_erase_block. The .nand
 * file IS the persistent store (writes go back in place); save_flash() just
 * flushes the dirty cache slots. */
#define NAND_PAGE_BYTES   528u
#define NAND0_PAGES       64u
#define NAND0_BYTES       (NAND0_PAGES*NAND_PAGE_BYTES)            /* 33792   */
#define NAND_BLOCK_PAGES  32u
#define NAND_BLOCK_BYTES  (NAND_BLOCK_PAGES*NAND_PAGE_BYTES)       /* 16896   */
#define NAND_CACHE_SLOTS  128u                                    /* ~2.1 MB */
#define NAND_TOTAL_BYTES  ((uint64_t)(65536u*2u+64u)*NAND_PAGE_BYTES)

static uint8_t  s_nand0[NAND0_BYTES];        /* pages 0..63, resident        */
static FILE    *s_nand_fp = nullptr;         /* the .nand main-array file     */
static uint64_t s_nand_main_bytes = 0;       /* num_nand_pages*528            */

typedef struct {
    int64_t  block;     /* main-region erase-block index, -1 = empty */
    uint8_t *data;      /* NAND_BLOCK_BYTES                          */
    bool     dirty;
    uint32_t lru;
} nand_slot_t;
static nand_slot_t s_slots[NAND_CACHE_SLOTS];
static uint32_t s_lru_clock = 0;
static bool s_nand_inited = false;
static nand_slot_t *s_last_slot = nullptr;   /* fast-path for repeat access */
static uint8_t s_nand_oom[NAND_BLOCK_BYTES];  /* fallback if a slot alloc fails */

static uint8_t *nb_alloc(size_t n){
#ifdef TAB5_PORT
    return (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return (uint8_t*)malloc(n);
#endif
}
static void nand_paging_init(){
    if(s_nand_inited) return;
    for(uint32_t i=0;i<NAND_CACHE_SLOTS;i++){
        s_slots[i].block=-1; s_slots[i].data=nullptr;
        s_slots[i].dirty=false; s_slots[i].lru=0;
    }
    s_last_slot=nullptr;
    s_nand_inited=true;
}
static void nand_flush_slot(nand_slot_t *s){
    if(s->block<0 || !s->dirty || !s_nand_fp){ s->dirty=false; return; }
    uint64_t off=(uint64_t)s->block*NAND_BLOCK_BYTES;
    if(fseek(s_nand_fp,(long)off,SEEK_SET)==0){
        fwrite(s->data,1,NAND_BLOCK_BYTES,s_nand_fp);
        fflush(s_nand_fp);
    }
    s->dirty=false;
}
static nand_slot_t* nand_get_block(int64_t block){
    /* Fast path: the 6502 reads/writes NAND sequentially, so the same block is
     * hit repeatedly — avoid the 128-slot linear scan in the common case. */
    if(s_last_slot && s_last_slot->block==block){ s_last_slot->lru=++s_lru_clock; return s_last_slot; }
    nand_slot_t *empty=nullptr,*lru=nullptr;
    for(uint32_t i=0;i<NAND_CACHE_SLOTS;i++){
        nand_slot_t *s=&s_slots[i];
        if(s->block==block){ s->lru=++s_lru_clock; s_last_slot=s; return s; }
        if(s->block<0){ if(!empty) empty=s; }
        else if(!lru || s->lru<lru->lru) lru=s;
    }
    nand_slot_t *s = empty? empty : lru;
    nand_flush_slot(s);
    if(!s->data){
        s->data=nb_alloc(NAND_BLOCK_BYTES);
        if(!s->data){ printf("[nand] slot alloc failed — degraded\n"); s->data=s_nand_oom; }
    }
    memset(s->data,0xff,NAND_BLOCK_BYTES);            /* pad past EOF with 0xff */
    uint64_t off=(uint64_t)block*NAND_BLOCK_BYTES;
    if(s_nand_fp && off < s_nand_main_bytes && fseek(s_nand_fp,(long)off,SEEK_SET)==0){
        size_t want=NAND_BLOCK_BYTES;
        if(off+want > s_nand_main_bytes) want=(size_t)(s_nand_main_bytes-off);
        fread(s->data,1,want,s_nand_fp);
    }
    s->block=block; s->dirty=false; s->lru=++s_lru_clock;
    s_last_slot=s;
    return s;
}
static inline uint8_t nb_get(uint32_t off){
    if(off < NAND0_BYTES) return s_nand0[off];
    uint64_t m=(uint64_t)off - NAND0_BYTES;
    int64_t block=m / NAND_BLOCK_BYTES;
    nand_slot_t *s=nand_get_block(block);
    return s->data[(size_t)(m - (uint64_t)block*NAND_BLOCK_BYTES)];
}
static inline void nb_set(uint32_t off, uint8_t v){
    if(off < NAND0_BYTES){ s_nand0[off]=v; return; }
    uint64_t m=(uint64_t)off - NAND0_BYTES;
    int64_t block=m / NAND_BLOCK_BYTES;
    nand_slot_t *s=nand_get_block(block);
    s->data[(size_t)(m - (uint64_t)block*NAND_BLOCK_BYTES)]=v;
    s->dirty=true;
}
/* Erase one 32-page (16896 B) block to 0xff. off is block-aligned (asserted by
 * the caller), so it never straddles a cache block or the nand0 boundary. */
static inline void nb_erase_block(uint32_t off){
    if(off + NAND_BLOCK_BYTES <= NAND0_BYTES){      /* resident boot block */
        memset(s_nand0 + off, 0xff, NAND_BLOCK_BYTES);
        return;
    }
    int64_t block=((uint64_t)off - NAND0_BYTES) / NAND_BLOCK_BYTES;
    nand_slot_t *s=nand_get_block(block);
    memset(s->data, 0xff, NAND_BLOCK_BYTES);
    s->dirty=true;
}
/* Flush every dirty cache slot to the .nand file (called from save_flash). */
void nand_flush_all(){
    for(uint32_t i=0;i<NAND_CACHE_SLOTS;i++) nand_flush_slot(&s_slots[i]);
}

void read_nand0_file(){
    nand_paging_init();
    memset(s_nand0,0xff,sizeof(s_nand0));
    FILE *f = fopen(nc2k_rom.nand0Path.c_str(), "rb");
    if(f==0) {
        /* Tab5 port: tolerate missing file (nc2k_load pre-checks) — blank block. */
        printf("file %s not found — blank nand0\n",nc2k_rom.nand0Path.c_str());
        nand_magic[0]=0;
        return;
    }
    fseek(f, 0, SEEK_END);
    long long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);  /* same as rewind(f); */
    if(fsize > (long long)sizeof(s_nand0)) fsize = sizeof(s_nand0);
    fread(s_nand0, fsize, 1, f);
    fclose(f);
    printf("<nand0_file_size=%lld>\n",fsize);
    for(int i=0;i<(int)sizeof(nand_magic)-1;i++){
        nand_magic[i]=s_nand0[0x200+0x10+i];
    }
    nand_magic[sizeof(nand_magic)-1]=0;
    printf("nand magic: %s\n",nand_magic);
}

void read_nand_file(){
    nand_paging_init();
    /* Open the (large) main NAND file read/write and demand-page it; do NOT
     * slurp it into RAM. The file is the persistent store. */
    s_nand_main_bytes = (uint64_t)num_nand_pages*NAND_PAGE_BYTES;
    if(s_nand_fp){ fclose(s_nand_fp); s_nand_fp=nullptr; }
    s_nand_fp = fopen(nc2k_rom.nandFlashPath.c_str(), "rb+");
    if(s_nand_fp==0) {
        /* Tab5 port: don't exit() (it reboot-loops). nc2k_load() pre-checks the
         * file exists, so this is just a safety net — run with a blank NAND. */
        printf("file %s not found — running with a blank NAND\n",nc2k_rom.nandFlashPath.c_str());
    } else {
        fseek(s_nand_fp, 0, SEEK_END);
        long long fsize = ftell(s_nand_fp);
        fseek(s_nand_fp, 0, SEEK_SET);
        printf("<nand_file_size=%lld>\n",fsize);
    }
    /* invalidate any stale cache slots from a previous load */
    for(uint32_t i=0;i<NAND_CACHE_SLOTS;i++){ s_slots[i].block=-1; s_slots[i].dirty=false; }
    s_last_slot=nullptr;

#if 0
    if(nc2000mode){
        if(!nc2000_use_2600_rom){
            //if it's not 2600 rom, then it should always be this
            memcpy(&nand[0][0]+0x200+0x10 /*512+16=528*/,"ggv nc2000",strlen("ggv nc2000"));
        }
        else{
            if(!nc2600_rom_use_ggvsim){
                //2600 physical nor expect this to be "ggv nc2010"
                //nc2kutil dump shows there is a '\n' or 0x0A. But this doesn't matter. It boots either with or without `\n`
                memcpy(&nand[0][0]+0x200+0x10 /*512+16=528*/,"ggv nc2010\n",strlen("ggv nc2010\n"));
            }else{
                memcpy(&nand[0][0]+0x200+0x10 /*512+16=528*/,"ggv nc2000",strlen("ggv nc2000"));
            }
        }
    }
    if(nc3000mode){
        memcpy(&nand[0][0]+0x200+0x10 /*512+16=528*/,"ggv nc3000",strlen("ggv nc3000"));
    }
#endif

}

void write_nand0_file(string file){
    if(!nc2000mode &&!nc3000mode) return;
    if(file.empty()) file=nc2k_rom.nand0Path;
    else file+=".nand0";
    FILE *f = fopen(file.c_str(), "wb");
    if(!f) return;
    fwrite(s_nand0, NAND0_BYTES, 1, f);
    fclose(f);
}

void write_nand_file(string file){
    if(!nc2000mode &&!nc3000mode) return;
    /* Live store: the .nand file is updated in place via the write-back cache,
     * so a normal save just flushes the dirty slots. (The "save as <file>"
     * snapshot path used by the desktop tool is not used on the device.) */
    if(file.empty()){
        nand_flush_all();
        return;
    }
}
void clear_nand_status(){
    nand_cmd.clear();
    nand_data.clear();
    nand_addr.clear();
    nand_read_cnt = 0;
}
uint8_t read_nand(){
    bool CLE = false;
    bool ALE = false;
    bool CE = false;
    if(nc3000mode){
        CLE = ram_io[0x18]&0x20;
        ALE = ram_io[0x18]&0x10;
        CE = ram_io[0x18]&0x04;
        if(CE) {
            if(debug_level>=1) printf("read while no CE\n");
        }
    }
    if(nc2000mode){
        CLE = ram_io[0x18]&0x01;
        ALE = ram_io[0x18]&0x02;
        CE = ram_io[0x18]&0x40;
        if(CE) {
            if(debug_level>=1) printf("read while no CE\n");
        }
    }
    if(CLE && ALE){
        if(debug_level>=1) printf("oops, in nand read, both CLE and ALE true!\n");
    }

    //printf("tick=%lld, read %x  %02x\n",tick, addr, ram_io[addr]);
    uint8_t roa_bbs=ram_io[0x0a];
    uint8_t ramb_vol=ram_io[0x0d];
    uint8_t bs=ram_io[0x00];
   ///////// uint16_t p=nc1020_states.cpu.reg_pc-4;

     if(enable_debug_nand) printf("tick=%llu read $29\n",tick%10000);

    if(nand_cmd.size()==0) {
        if(debug_level>=1) printf("oops! no nand cmd %d %d %d\n",CLE,ALE,CE);
        return 0xff;
    }
    assert(nand_cmd.size()>0);

    /*
        special handle of read status after a long time
    */
    if(nand_cmd[0]==0x70 && nand_cmd.size()==1 && nand_addr.size()==0 &&nand_data.size()==0) {
        clear_nand_status();
        return 0x40;
    }

    if(nand_cmd[0]==0x90 &&nand_cmd.size()==1 && nand_addr.size()==1 && nand_addr[0]==0x00 &&nand_data.size()==0) {
        if(nand_read_cnt==0) {
            nand_read_cnt++;
            return 0xec;
        }
        if(nand_read_cnt==1) {
            clear_nand_status();
            return 0x75;
        }
        assert(false);
        return 0;
    }

    /*
        robust check
    */
    if(nand_cmd[0]!=0x0 && nand_cmd[0]!=0x1 &&nand_cmd[0]!=0x60 &&nand_cmd[0]!=0x50){
        printf("<<%x>>!!!\n",(unsigned char)nand_cmd[0]);
        for(int i=0;i<nand_cmd.size();i++){
            printf("<%x>",(unsigned char)nand_cmd[i]);
        }
        printf("\n");
        assert(false);
        return 0;
    }

    /*
        read low/high and read spare
    */
    unsigned char cmd=nand_cmd[0];
    if(cmd ==0 ||cmd==1||cmd==0x50){
        if(nand_cmd.size()!=1 || nand_addr.size()!=4  ||nand_data.size()!=0){
            printf("oops cmd size!=5\n");
            for(int i=0;i<nand_cmd.size();i++){
                printf("<%x>",(unsigned char)nand_cmd[i]);
            }
            printf("\n");

            /*if(nand_cmd.size()==1 && nand_cmd[0]==0x0){
                printf("oops!! nand_cmd=[0] but trying to read\n");
                return 0xff;
            }*/
            assert(false);
            return 0xff;
        }

        assert(nand_cmd.size()==1 && nand_addr.size()==4 && nand_data.size()==0);
        unsigned char low=nand_addr[0];
        unsigned char mid=nand_addr[1];
        unsigned char high=nand_addr[2];
        unsigned char a25=nand_addr[3]&0x01;

        uint32_t pos=a25*256u*256u+   high*256u+mid;

        /*
        if(nand_cmd.size()==5&&false){
            printf("[%x %x]",low,high);
            
            for(int i=0;i<nand_cmd.size();i++){
                printf("<%x>",(unsigned char)nand_cmd[i]);
            }
            printf("\n");
            exit(-1);
            //printf("<%x;%x,%x:%x,%d>", final, pos, low,cmd,nand_read_cnt);
        }*/
        
        unsigned int x=pos;
        unsigned int y=low;
        //unsigned int pre_final= pos*528u+ y +nand_read_cnt;
        //assert(pre_final%528==0);
        if(cmd==0x1) y+=256u;
        if(cmd==0x50) y+=512u;
        unsigned int final= pos*528u+ y +nand_read_cnt;
        if(nand_read_cnt!=0||cmd!=0){
            //assert(final%528!=0);
            if(final%528==0) if(debug_level>=1) printf("warn: read %04x accross 528 boundary\n",final);
        }


        //final-=32*1024;
        if(nand_read_cnt==0 && enable_debug_nand){
            printf("[%x %x]",low,high);
            
            for(int i=0;i<nand_cmd.size();i++){
                printf("<%x>",(unsigned char)nand_cmd[i]);
            }
            printf("<%x;%x,%x:%x,%d>\n", (unsigned)final, (unsigned)pos, (unsigned)low, (unsigned)cmd, nand_read_cnt);
        }
        uint8_t result=nb_get(final);
        //if(final<0) return 0x00;
        //uint8_t result=nand[pos][low+off+nand_read_cnt];
        nand_read_cnt++;
        //printf("<<%02x>>",result);
        return result;
    }

    assert(false);
    return 0;
}

void debug_show_nand_cmd(){
    if(enable_debug_nand)
    {
        for(int i=0;i<nand_cmd.size();i++){
            printf("<%02x>",(unsigned char)nand_cmd[i]);
        }
        printf("\n");
    }
}
void nand_write(uint8_t value){
    bool CLE = false;
    bool ALE = false;
    bool CE = false;
    if(nc3000mode){
        CLE = ram_io[0x18]&0x20;
        ALE = ram_io[0x18]&0x10;
        CE = ram_io[0x18]&0x04;
    }
    if(nc2000mode){
        CLE = ram_io[0x18]&0x01;
        ALE = ram_io[0x18]&0x02;
        CE = ram_io[0x18]&0x40;
    }
    if(CLE && ALE){
        if(debug_level>=1) printf("oops, in nand write, both CLE and ALE true!\n");
        return;
    }

    //printf("tick=%llu write $29 %x  CLE=%d ALE=%d %d\n",tick%10000,value,CLE,ALE,(int)nand_cmd.size());
    if(enable_debug_nand) printf("tick=%llu write $29 %x  CLE=%d ALE=%d\n",tick%10000,value,CLE,ALE);
    //printf("tick=%lld, write %x  %02x\n",tick, addr, value);
    uint8_t roa_bbs=ram_io[0x0a];
    uint8_t ramb_vol=ram_io[0x0d];
    uint8_t bs=ram_io[0x00];

    if(CLE){
        //note: the datasheet says 0xff doesn't need CLE, but in wqx code seems like CLE is always enabled when 0xff is used
        if(value ==0xff || value == 0x00|| value==0x01 || value ==0x50 ||value==0x60||value ==0x70||value==0x90){
            debug_show_nand_cmd();
            if(nand_cmd.size()>0){
                if(nand_cmd.size()==1 && nand_addr.size()==4 && nand_data.size()==0) assert(nand_cmd[0]==0x00||nand_cmd[0]==0x01||nand_cmd[0]==0x50);
                else if(nand_cmd.size()==2 && nand_addr.size()==3 &&nand_data.size()==0) assert(nand_cmd[0]==0x60);
                else assert(false);
            }
            clear_nand_status();
            if(value!=0xff){
                nand_cmd.push_back(value);
            }
            goto out;
        }
        if(value ==0x10) {
            if(nand_cmd[0]==0x50 && nand_cmd.size()== 2&& nand_addr.size()==4 && nand_data.size()==16) {
                assert(nand_cmd[1]==0x80);

                unsigned char low=nand_addr[0];
                unsigned char mid=nand_addr[1];
                unsigned char high=nand_addr[2];
                unsigned char a25=nand_addr[3]&0x01;

                uint32_t pos=a25*256u*256u+high*256u+mid;

                unsigned int x=pos;
                unsigned int y=low;
                unsigned int final= pos*528u+ y +512;

                assert((final-512)%(528)==0);

                bool warn=false;
                for(int i=0;i<16;i++){
                    uint8_t cur=nb_get(final+i);
                    if(cur!=0xff){
                        warn=true;
                        //this is allowed, but wqx's software always erase before write
                        if(forced_erase_before_write) cur=0xff;
                    }
                    cur&=nand_data[i];
                    nb_set(final+i,cur);
                }
                if(warn){
                    if(debug_level>=1) printf("oops writing to non-erased byte at %x!!!!!!!!!!\n",final);
                }
                printf("[nand] program spare, offset=%x\n",final);
                clear_nand_status();
            }
            else if(nand_cmd[0]==0x0 && nand_cmd.size()==2 && nand_addr.size()==4 && nand_data.size()==528){
                assert(nand_cmd[1]==0x80);

                unsigned char low=nand_addr[0];
                unsigned char mid=nand_addr[1];
                unsigned char high=nand_addr[2];
                unsigned char a25=nand_addr[3]&0x01;

                uint32_t pos=a25*256u*256u+high*256u+mid;

                unsigned int x=pos;
                unsigned int y=low;
                unsigned int final= pos*528u+ y;
                assert(final%(528)==0);
                printf("[nand] program, offset=%x\n",final);

                bool warn=false;
                for(int i=0;i<528;i++){
                    uint8_t cur=nb_get(final+i);
                    if(cur!=0xff){
                        warn=true;
                        //this is allowed, but wqx's software always erase before write
                        if(forced_erase_before_write) cur=0xff;
                    }
                    cur&=nand_data[i];
                    nb_set(final+i,cur);
                }
                if(warn){
                    if(debug_level>=1) printf("oops writing to non-erased byte at %x!!!!!!!!!!\n",final);
                }
                clear_nand_status();
            }
            else{
                debug_show_nand_cmd();
                printf("unexpected situation for cmd 0x10 %d",(int)nand_cmd.size());
                assert(false);
            }
            goto out;
        }
        if(value==0xd0||value==0x80){
            if(value==0xd0){
                assert(nand_cmd.size()==1);
                assert(nand_cmd[0]==0x60);
                assert(nand_addr.size()==3);
                assert(nand_data.size()==0);

                unsigned char low=nand_addr[0];
                unsigned char mid=nand_addr[1];
                unsigned char high=nand_addr[2]&0x01;

                unsigned int final= (high*256u*256u + mid*256u+low)*528u;

                nand_read_cnt++;
                printf("[nand] erase, offset=%x\n",final);

                assert(final%(32*528)==0);
                assert((uint64_t)final + NAND_BLOCK_BYTES <= NAND_TOTAL_BYTES);
                nb_erase_block(final);
            }
            if(value==0x80){
                assert(nand_cmd.size()>=1);
                assert(nand_cmd[0]==0x50||nand_cmd[0]==0x00);
                assert(nand_addr.size()==0);
                assert(nand_data.size()==0);
            }
            nand_cmd.push_back(value);
            goto out;
        }
        assert(false);
    }

    if(ALE){
        if(nand_cmd.size()==0){
            printf("got addr %02x while nand_cmd is empty\n",value);
            assert(false);
        }

        nand_addr.push_back(value);
        goto out;
    }
 

    if(nand_cmd.size()!=0) {
        if(nand_cmd[0]==0x50) {
            assert(nand_cmd.size()>=2);
            assert(nand_cmd[1]==0x80);
            assert(nand_cmd.size()<22);
        }else if (nand_cmd[0]==0x00){
            assert(nand_cmd.size()>=2);
            assert(nand_cmd[1]==0x80);
            assert(nand_cmd.size()<534);
        }else{
            assert(false);
        }
        nand_data.push_back(value);
    }
    else{
        for(int i=0;i<nand_cmd.size();i++){
            printf("<%02x>",nand_cmd[i]);
        }
        printf("[%02x]\n",value);
        printf("got data %02x while nand_cmd is empty\n",value);
        assert(false);
    }
    goto out;

    out:;

    // the out label here is for put some print cmd for debug

    //if(nand_cmd.size()==1&& nand_cmd[0]==0xff) nand_cmd.clear();
    //printf("bs=%x roa_bbs=%x pc=%x  %x %x %x %x \n",ram_io[0x00], ram_io[0x0a], reg_pc,  Peek16(p), Peek16(p+1),Peek16(p+2),Peek16(p+3));
    //if(do_inject) wanna_inject=true;
}
