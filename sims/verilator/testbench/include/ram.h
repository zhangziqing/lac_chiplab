#ifndef CHIPLAB_RAM_H
#define CHIPLAB_RAM_H

#include "common.h"
#include "cpu_tool.h"
#include "devices.h"
#include "rand64.h"
#include "uart.h"
#include <vector>
#include <tuple>

using std::vector;
using std::tuple;

class RamSection {
public:
    vluint64_t  tag;
    unsigned char* data;
};

class CpuRam: CpuTool {
public:
    FILE* mem_out;
    char mem_out_path[128];
    static const int tbwd = 8;
    static const int pgwd = 20;
    static const int tbsz = 1<<tbwd;
    static const int pgsz = 1<<pgwd;
    static const vluint64_t tbmk = ~((1<<(tbwd+pgwd))-1);
    vector<RamSection> mem[tbsz];
    vector<RamSection>::iterator cur[tbsz];
    Rand64* rand64;
    CpuDevices dev;

    bool ram_read_mark = false;
    int debug = 0;

    int dead_clk = 0;
    
    int read_valid;
    vluint64_t read_addr ;

    inline int find(vluint64_t ptr){
        vluint64_t idx = (ptr>>pgwd)&(tbsz-1);
        vluint64_t tag = ptr&tbmk;
        return find(tag,idx);
    }

    inline void jump(vluint64_t ptr){
        vluint64_t idx = (ptr>>pgwd)&(tbsz-1);
        vluint64_t tag = ptr&tbmk;
        jump(tag,idx);
    }

    int find(vluint64_t tag,vluint64_t idx);

    void jump(vluint64_t tag,vluint64_t idx);

    CpuRam(Vtop* top,Rand64* rand64,vluint64_t main_time,struct UART_STA *uart_status,const char*mem_path);
    ~CpuRam();


    inline vluint64_t encwm32(const unsigned e) const {
        vluint64_t m = 0;
        if((e&0xf)==0xf)m|=0xffffffff;
        else if(e&0xf){
            m|= (e&0x1)?0x000000ff:0;
            m|= (e&0x2)?0x0000ff00:0;
            m|= (e&0x4)?0x00ff0000:0;
            m|= (e&0x8)?0xff000000:0;
        }
        return m;
    }

    inline vluint64_t encwm64(const unsigned e) const {
        return encwm32(e)|(encwm32(e>>4)<<32);
    }

    unsigned read32(vluint64_t a);
    vluint64_t read64(vluint64_t a);
    void write64(vluint64_t a,vluint64_t m,vluint64_t d);
    void write32(vluint64_t a,vluint64_t m,unsigned d);

    inline void write4B(vluint64_t a,vluint64_t m,unsigned d){write32(a,encwm32(m),d);}
    inline void write8B(vluint64_t a,vluint64_t m,vluint64_t d){write64(a,encwm64(m),d);}
    inline void write16B(vluint64_t a,vluint64_t m,unsigned* d){
        write32(a   ,encwm32(m    ),d[0]);
        write32(a+ 4,encwm32(m>> 4),d[1]);
        write32(a+ 8,encwm32(m>> 8),d[2]);
        write32(a+12,encwm32(m>>12),d[3]);
    }
    inline void read16B(vluint64_t a,unsigned* d){
        d[0] = read32(a   );
        d[1] = read32(a+ 4);
        d[2] = read32(a+ 8);
        d[3] = read32(a+12);
    }

    int process(vluint64_t main_time);

    /* breakpoint */
	int breakpoint_save(vluint64_t main_time, const char* brk_file_name, struct UART_STA *uart_status);
	int breakpoint_restore(vluint64_t main_time,  const char* brk_file_name, struct UART_STA *uart_status);

    #ifdef RAND_TEST
    int process_rand(vluint64_t main_time) {
        int cpu_ex = top->rand_test_bus[RAND_BUS_CPU_EX];
        int eret   = top->rand_test_bus[RAND_BUS_ERET];
        int excode = top->rand_test_bus[RAND_BUS_EXCODE];
        int commit_num     = top->rand_test_bus[RAND_BUS_COMMIT_NUM];
        int cmt_last_split = top->rand_test_bus[RAND_BUS_CMT_LAST_SPLIT];
        int cpu_ex_next = rand64->cpu_ex;
        int tlb_ex_next = rand64->tlb_ex;
        #ifdef RAND32
        long long bad_vaddr = (long long)top->rand_test_bus[RAND_BUS_BADVADDR];
        #else 
        long long bad_vaddr = (long long)top->rand_test_bus[RAND_BUS_BADVADDR] | ((long long)top->rand_test_bus[RAND_BUS_BADVADDR+1]<<32);
        #endif
        long long gr_rtl[32];

        // get gr value from rtl
        gr_rtl[0] = 0;
        for (int i=1;i<32;i++) {
            #ifdef RAND32
            gr_rtl[i] = (long long)top->rand_test_bus[i+RAND_BUS_GR_RTL];
            #else
            gr_rtl[i] = (long long)top->rand_test_bus[2*i+RAND_BUS_GR_RTL] + ((long long)top->rand_test_bus[2*i+1]<<32+RAND_BUS_GR_RTL);
            #endif
            //printf("gr rt[%02d] = %08llx\n",i,gr_rtl[i]);
        }
        
        if (rand64->tlb_ex) {
            printf("=========================================================\n");
            printf("rand64 c++ version tlb refill start\n");
            printf("Looking for this address: %llx\n",bad_vaddr);
            if (rand64->tlb_refill_once(bad_vaddr)) {
                printf("Error when tlb refill\n");
                fprintf(rand64->result_flag, "RUN FAIL!\n");
                return 1;
            }
            int local_num = rand64->tlb->v0 + rand64->tlb->v1;
            printf("Found %d entry\n",local_num);
            tlb_ex_next = 0;
            printf("=========================================================\n");
        }
        // skip check if under cpu_ex or the last commit is splitted
        // Note. multiple issue core might commit several value with a new ex occured in one clock.
        if (!rand64->cpu_ex&&!rand64->last_split) {
            if(rand64->compare(gr_rtl)) {
                printf("REGSTER NOT MATCH!!!\n");
                fprintf(rand64->result_flag, "RUN FAIL!\n");
                rand64->print_ref(gr_rtl);
                return 1;
            }
        }

        if (eret) {
            cpu_ex_next = 0;
            tlb_ex_next = 0;
            printf("\nBegin compare\n\n");
        } else if (cpu_ex) {
            printf("CPU EX\n");
            printf("Main time = %d\n",main_time);
            cpu_ex_next = 1;
            if (excode == EX_SYSCALL) {
                printf("SYSCALL DETECTED\n");
                printf("Rand TEST END\n");
                fprintf(rand64->result_flag, "RUN PASS!\n");
                return 1;
            } else if (excode == EX_TLBR) {
                printf("TLB EX\n");
                tlb_ex_next = 1;
            }
            else {
                printf("CPU unexpect EX\n");
                printf("Random Test End\n");
                fprintf(rand64->result_flag, "RUN FAIL!\n");
                return 1;
            }
        }

        if (!rand64->cpu_ex&&!eret) {
            rand64->update(commit_num, main_time);
        }

        rand64->cpu_ex = cpu_ex_next;
        rand64->tlb_ex = tlb_ex_next;
        rand64->last_split = cmt_last_split;
             
        if(commit_num == 0){
            dead_clk++; 
        }
        else{
            dead_clk = 0;
        }

        if(dead_clk > 10000){
            printf("CPU status no change for 10000 clocks, simulation must exist error!!!!\n");
            printf("Random Test End\n");
            fprintf(rand64->result_flag, "RUN FAIL!\n");
            return 1;
        }
    
        return 0;
    }
    #endif

    int special_read();

    //128/256
    void process_read64_same(vluint64_t data, unsigned* d);
    //64
    void process_read64_same(vluint64_t data, vluint64_t &d);
    //128
    void process_read32_same(vluint64_t data, unsigned* d);
    //64
    void process_read32_same(vluint64_t data, vluint64_t &d);
    //32
    void process_read32_same(vluint64_t data, unsigned int &d);

    void process_read128(vluint64_t main_time,vluint64_t a,unsigned* d);
    int process_write128(vluint64_t main_time,vluint64_t a,vluint64_t m,unsigned* d);

    // 128/256
    void process_read(vluint64_t main_time,vluint64_t a,unsigned* d);
    //64
    void process_read(vluint64_t main_time,vluint64_t a,vluint64_t &d);
    void process_read(vluint64_t main_time,vluint64_t a,unsigned int &d);

    int process_write(vluint64_t main_time,vluint64_t a,vluint64_t m,unsigned* d);
    int process_write(vluint64_t main_time,vluint64_t a,vluint64_t m,vluint64_t d);
    int process_write(vluint64_t main_time,vluint64_t a,vluint64_t m,unsigned int d);

};

#endif  // CHIPLAB_RAM_H