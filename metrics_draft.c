/*
 * Bagian 3, 4, 5 - Scheduling Table, Rata-rata, CPU Util & Throughput
 */
#include <stdio.h>

#define LINE "======================================================================="
#define DASH "-----------------------------------------------------------------------"

typedef enum {
    NEW,
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
} ProcessState;

typedef struct {
    int pid;

    // Original process data
    int arrival_time;
    int burst_time;

    // Runtime data
    int remaining_time;
    int queue_level;
    ProcessState state;

    // Scheduling metrics
    int first_start_time;
    int completion_time;
    int turnaround_time;
    int waiting_time;
    int response_time;
} PCB;

/* Hitung TAT, WT, RT lalu SIMPAN ke PCB.
 * Dipanggil sekali setelah simulasi selesai (semua proses TERMINATED). */
void hitung_metrics(PCB p[], int n) {
    for (int i = 0; i < n; i++) {
        /* TAT = CT - AT */
        p[i].turnaround_time = p[i].completion_time - p[i].arrival_time;   

        /* WT  = TAT - BT */
        p[i].waiting_time    = p[i].turnaround_time - p[i].burst_time;  
        
        /* RT  = start - AT */
        p[i].response_time   = p[i].first_start_time - p[i].arrival_time;  
    }
}

/* Bagian 3: tabel hasil scheduling */
void print_scheduling_table(PCB p[], int n) {
    printf("%s\n%-27sSCHEDULING TABLE\n%s\n", LINE, "", LINE);
    printf("%-6s%-6s%-6s%-6s%-6s%-6s%-6s%s\n",
           "PID", "AT", "BT", "CT", "TAT", "WT", "RT", "Final Q");
    printf("%s\n", DASH);
    for (int i = 0; i < n; i++) {
        printf("P%-5d%-6d%-6d%-6d%-6d%-6d%-6dQ%d\n",
               p[i].pid, p[i].arrival_time, p[i].burst_time,
               p[i].completion_time, p[i].turnaround_time,
               p[i].waiting_time, p[i].response_time, p[i].queue_level);
    }
    printf("%s\n\n", LINE);
}

/* Bagian 4: rata-rata WT, TAT, RT */
void print_averages(PCB p[], int n) {
    double sum_wt = 0, sum_tat = 0, sum_rt = 0;
    for (int i = 0; i < n; i++) {
        sum_wt  += p[i].waiting_time;
        sum_tat += p[i].turnaround_time;
        sum_rt  += p[i].response_time;
    }
    printf("%s\n%-24sSCHEDULING PERFORMANCE\n%s\n", LINE, "", LINE);
    printf("Average Waiting Time    : %.2f\n", sum_wt / n);
    printf("Average Turnaround Time : %.2f\n", sum_tat / n);
    printf("Average Response Time   : %.2f\n\n", sum_rt / n);
}

/* Bagian 5: CPU Utilization & Throughput
 * Busy time  = total BT asli semua proses
 * Total time = CT paling akhir (simulasi mulai t=0, idle ikut terhitung) */
void print_util_throughput(PCB p[], int n) {
    int busy = 0, total = 0;
    for (int i = 0; i < n; i++) {
        busy += p[i].burst_time;
        if (p[i].completion_time > total) total = p[i].completion_time;
    }
    printf("%s\n%-20sCPU UTILIZATION AND THROUGHPUT\n%s\n", LINE, "", LINE);
    printf("CPU Utilization : %.2f%%\n", (double)busy / total * 100.0);
    printf("Throughput      : %.2f process/time unit\n\n", (double)n / total);
}

/* ===== main DUMMY untuk tes, ntar bakal dihapus pass digabung ===== */
int main(void) {
    PCB p[4] = {0};
    int data[4][5] = {
        {1, 0, 8, 19, 0}, {2, 1, 4, 13, 2}, {3, 2, 2, 6, 4}, {4, 3, 5, 16, 6}
    };
    int final_q[4] = {2, 1, 0, 1};
    for (int i = 0; i < 4; i++) {
        p[i].pid = data[i][0];
        p[i].arrival_time = data[i][1];
        p[i].burst_time = data[i][2];
        p[i].completion_time = data[i][3];
        p[i].first_start_time = data[i][4];
        p[i].queue_level = final_q[i];
        p[i].remaining_time = 0;
        p[i].state = TERMINATED;
    }

    hitung_metrics(p, 4);
    print_scheduling_table(p, 4);
    print_averages(p, 4);
    print_util_throughput(p, 4);
    return 0;
}