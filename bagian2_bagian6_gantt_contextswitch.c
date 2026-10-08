/* ============================================================
 * bagian2_bagian6_gantt_contextswitch.c
 * Bagian: 2 - Gantt Chart / CPU Execution Timeline
 *          6 - Context Switch
 *
 * CARA INTEGRASI DENGAN LOOP SCHEDULER (Anggota 1):
 * Setiap kali scheduler kasih CPU ke suatu proses untuk satu
 * "slice" (dari waktu mulai dia jalan sampai dia berhenti jalan,
 * entah karena selesai, kena preempt, atau quantum habis),
 * panggil:
 *
 *     log_add_slice(&log, pid, queue_level, waktu_mulai, waktu_selesai);
 *
 * Contoh: P1 jalan dari t=0 sampai t=3, lalu di-preempt oleh P2:
 *     log_add_slice(&log, 1, 0, 0, 3);
 *
 * Setelah seluruh simulasi selesai, panggil:
 *     print_gantt_chart(&log);
 *     print_context_switch_info(&log);
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>

#define MAX_SLICES 1000
#define MAX_QUEUE_LEVEL 3  


typedef struct {
    int pid;
    int queue_level;   
    int start_time;
    int end_time;
} ExecutionSlice;

typedef struct {
    ExecutionSlice slices[MAX_SLICES];
    int count;
} ExecutionLog;

void log_init(ExecutionLog *log) {
    log->count = 0;
}

void log_add_slice(ExecutionLog *log, int pid, int queue_level, int start_time, int end_time) {
    if (log->count >= MAX_SLICES) {
        fprintf(stderr, "Execution log penuh (maks %d slice).\n", MAX_SLICES);
        return;
    }
    ExecutionSlice *s = &log->slices[log->count];
    s->pid = pid;
    s->queue_level = queue_level;
    s->start_time = start_time;
    s->end_time = end_time;
    log->count++;
}

/* BAGIAN 2 - GANTT CHART / CPU EXECUTION TIMELINE */

void print_gantt_chart(ExecutionLog *log) {
    printf("=================================================================\n");
    printf("CPU EXECUTION TIMELINE (GANTT CHART)\n");
    printf("=================================================================\n\n");

    if (log->count == 0) {
        printf("(belum ada proses yang dijalankan)\n\n");
        return;
    }

    printf("|");
    for (int i = 0; i < log->count; i++) {
        printf(" P%-3d|", log->slices[i].pid);
    }
    printf("\n");

    printf("%d", log->slices[0].start_time);
    for (int i = 0; i < log->count; i++) {
        printf("%*d", 6, log->slices[i].end_time);
    }
    printf("\n\n");
}

/* BAGIAN 6 - CONTEXT SWITCH  */


int hitung_total_context_switch(ExecutionLog *log) {
    int total = 0;
    for (int i = 1; i < log->count; i++) {
        if (log->slices[i].pid != log->slices[i - 1].pid) {
            total++;
        }
    }
    return total;
}


void hitung_breakdown_per_queue(ExecutionLog *log, int breakdown[MAX_QUEUE_LEVEL]) {
    for (int q = 0; q < MAX_QUEUE_LEVEL; q++) {
        breakdown[q] = 0;
    }
    for (int i = 1; i < log->count; i++) {
        if (log->slices[i].pid != log->slices[i - 1].pid) {
            int q = log->slices[i].queue_level;
            if (q >= 0 && q < MAX_QUEUE_LEVEL) {
                breakdown[q]++;
            }
        }
    }
}

void print_context_switch_info(ExecutionLog *log) {
    int total = hitung_total_context_switch(log);
    int breakdown[MAX_QUEUE_LEVEL];
    hitung_breakdown_per_queue(log, breakdown);

    printf("=================================================================\n");
    printf("CONTEXT SWITCH INFORMATION\n");
    printf("=================================================================\n\n");
    printf("Total Context Switch : %d\n\n", total);

    printf("Breakdown per Queue:\n");
    for (int q = 0; q < MAX_QUEUE_LEVEL; q++) {
        printf("  Q%d : %d\n", q, breakdown[q]);
    }
    printf("\n");
}

int main() {
    ExecutionLog log;
    log_init(&log);

   
    log_add_slice(&log, 1, 0, 0, 2);
  
    log_add_slice(&log, 2, 0, 2, 4);
   
    log_add_slice(&log, 3, 0, 4, 8);
   
    log_add_slice(&log, 4, 0, 8, 10);

    log_add_slice(&log, 1, 1, 10, 11);

    print_gantt_chart(&log);
    print_context_switch_info(&log);

    return 0;
}