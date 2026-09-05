#!/usr/bin/env python3
"""Execute the actual recovery builder: rejected I/O must not mutate pinned PRPs."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/nvme.c').read_text()
start=source.index('static bool nvme_rw_blocks(')
end=source.index('\nbool nvme_read_blocks(', start)
body=source[start:end]
harness=r'''
#include "ans_queue.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
typedef uint64_t u64;
typedef uint32_t u32;
#define SZ_4K 4096
#define NVME_MAX_IO_BLOCKS 32
#define NVME_CMD_WRITE 1
#define NVME_CMD_READ 2
static bool nvme_initialized=true;
static struct ans_queue_engine queue_engine;
static u64 io_prp_list[512];
static int ioq;
static unsigned writes,execs;
static void dma_wmb(void) {writes++;}
static bool nvme_exec_command(void *q,struct nvme_command *c,u64 *r)
{(void)q;(void)c;(void)r;execs++;return false;}
'''+body+r'''
int main(void) {
    u64 saved[512];
    for (unsigned i=0;i<512;i++) saved[i]=io_prp_list[i]=0x11000+i*4096;
    queue_engine.faulted=true;
    assert(!nvme_rw_blocks(1,0,3,(void *)(uintptr_t)0x20000,false));
    assert(!memcmp(saved,io_prp_list,sizeof(saved)));
    assert(writes==0 && execs==0);
    queue_engine.faulted=false;queue_engine.outstanding=1;
    assert(!nvme_rw_blocks(1,0,3,(void *)(uintptr_t)0x20000,false));
    assert(!memcmp(saved,io_prp_list,sizeof(saved)));
    assert(writes==0 && execs==0);
    queue_engine.outstanding=0;
    assert(!nvme_rw_blocks(1,0,3,(void *)(uintptr_t)0x20000,false));
    assert(io_prp_list[0]==0x21000 && io_prp_list[1]==0x22000);
    assert(writes==1 && execs==1);
}
'''
with tempfile.TemporaryDirectory(prefix='ans-prp-quarantine-') as d:
    c=Path(d)/'test.c';exe=Path(d)/'test';c.write_text(harness)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-I',str(ROOT/'src'),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('nvme_prp_quarantine_test: PASS')
