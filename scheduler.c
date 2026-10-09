/*
 * Bagian 3, 4, 5 - Scheduling Table, Rata-rata, CPU Util & Throughput
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    int first_start_time;   /* -1 = belum pernah mendapat CPU */
    int completion_time;
    int turnaround_time;
    int waiting_time;
    int response_time;
} PCB;

/*
 * BAGIAN 7 DAN OUTPUT KHUSUS MLFQ
 *
 * Tempel bagian ini SETELAH definisi ProcessState dan PCB di scheduler.c.
 * Tidak ada main(), input, queue, atau loop scheduler di file ini.
 */

#define MAX_STATE_LOG 1000
#define MAX_MIGRATION_LOG 500
#define MAX_PREEMPTION_LOG 500

/* ======================== DATA HISTORY =========================
 * Struct berikut BUKAN pengganti PCB. Struct ini hanya menyimpan riwayat
 * yang dibutuhkan oleh output bagian 7 dan output khusus MLFQ.
 *
 * PCB hanya menyimpan state dan queue proses saat ini. Kalau state berubah,
 * nilai lama di PCB tertimpa. Karena itu, diperlukan log terpisah.
 */

/* Menyimpan satu perubahan state milik sebuah proses. */
typedef struct {
    int pid;
    ProcessState state;
    int time;
    int queue_level;
} StateLog;

/* Menyimpan perpindahan proses dari satu queue ke queue lain. */
typedef struct {
    int pid;
    int from_queue;
    int to_queue;
    int time;
    int remaining_time;
} MigrationLog;

/* Menyimpan hasil preemption oleh queue yang lebih tinggi. */
typedef struct {
    int time;
    int stopped_pid;
    int stopped_queue;
    int stopped_remaining_time;
    int incoming_pid;
    int incoming_queue;
} PreemptionLog;

static StateLog state_logs[MAX_STATE_LOG];
static MigrationLog migration_logs[MAX_MIGRATION_LOG];
static PreemptionLog preemption_logs[MAX_PREEMPTION_LOG];

static int state_log_count = 0;
static int migration_log_count = 0;
static int preemption_log_count = 0;

/* Kosongkan history sebelum satu simulasi/test case dimulai. */
void reset_mlfq_logs(void) {
    state_log_count = 0;
    migration_log_count = 0;
    preemption_log_count = 0;
}

/* Ubah nilai enum ProcessState menjadi teks agar dapat dicetak. */
const char *process_state_name(ProcessState state) {
    switch (state) {
        case NEW:        return "NEW";
        case READY:      return "READY";
        case RUNNING:    return "RUNNING";
        case BLOCKED:    return "BLOCKED";
        case TERMINATED: return "TERMINATED";
        default:         return "UNKNOWN";
    }
}

/*
 * Catat state terbaru proses ke history tanpa mengubah isi PCB.
 * Fungsi dipanggil oleh scheduler setelah field state diperbarui, saat:
 * - proses datang                 : NEW -> READY
 * - proses mendapat CPU          : READY -> RUNNING
 * - proses dipreempt             : RUNNING -> READY
 * - proses menunggu I/O (jika ada): RUNNING -> BLOCKED
 * - proses selesai               : RUNNING -> TERMINATED
 */
void record_process_state(const PCB *process, int time) {
    if (state_log_count >= MAX_STATE_LOG) {
        printf("State log penuh.\n");
        return;
    }

    state_logs[state_log_count].pid = process->pid;
    state_logs[state_log_count].state = process->state;
    state_logs[state_log_count].time = time;
    state_logs[state_log_count].queue_level = process->queue_level;
    state_log_count++;
}

/*
 * Catat perpindahan queue tanpa mengubah queue_level atau state di PCB.
 * Scheduler memanggil fungsi ini ketika quantum Q0 atau Q1 habis dan proses
 * belum selesai, sebelum/ketika proses dipindahkan ke queue tujuan.
 */
void record_queue_migration(const PCB *process,
                            int source_queue,
                            int destination_queue,
                            int time) {
    if (migration_log_count >= MAX_MIGRATION_LOG) {
        printf("Migration log penuh.\n");
        return;
    }

    migration_logs[migration_log_count].pid = process->pid;
    migration_logs[migration_log_count].from_queue = source_queue;
    migration_logs[migration_log_count].to_queue = destination_queue;
    migration_logs[migration_log_count].time = time;
    migration_logs[migration_log_count].remaining_time =
        process->remaining_time;
    migration_log_count++;
}

/*
 * Catat jika proses pada queue rendah dihentikan karena ada proses READY
 * pada queue yang lebih tinggi. Preemption tidak mengubah queue_level.
 */
void record_higher_queue_preemption(const PCB *stopped,
                                    const PCB *incoming,
                                    int time) {
    if (preemption_log_count >= MAX_PREEMPTION_LOG) {
        printf("Preemption log penuh.\n");
        return;
    }

    preemption_logs[preemption_log_count].time = time;
    preemption_logs[preemption_log_count].stopped_pid = stopped->pid;
    preemption_logs[preemption_log_count].stopped_queue =
        stopped->queue_level;
    preemption_logs[preemption_log_count].stopped_remaining_time =
        stopped->remaining_time;
    preemption_logs[preemption_log_count].incoming_pid = incoming->pid;
    preemption_logs[preemption_log_count].incoming_queue =
        incoming->queue_level;
    preemption_log_count++;
}

/* Bagian 7: tampilkan perubahan state setiap proses. */
void print_process_state_transitions(const PCB processes[], int n) {
    printf("%s\n", LINE);
    printf("PROCESS STATE TRANSITIONS\n");
    printf("%s\n", LINE);

    for (int i = 0; i < n; i++) {
        printf("P%d : NEW", processes[i].pid);

        for (int j = 0; j < state_log_count; j++) {
            if (state_logs[j].pid != processes[i].pid) {
                continue;
            }

            printf(" -> %s", process_state_name(state_logs[j].state));

            if (state_logs[j].state == READY ||
                state_logs[j].state == RUNNING ||
                state_logs[j].state == BLOCKED) {
                printf(" Q%d", state_logs[j].queue_level);
            }

            printf(" (t=%d)", state_logs[j].time);
        }

        printf("\n");
    }

    printf("%s\n\n", LINE);
}

/* Output Khusus MLFQ: tampilkan ringkasan queue setiap proses. */
void print_process_queue_movements(const PCB processes[], int n) {
    printf("%s\n", LINE);
    printf("PROCESS / QUEUE MOVEMENTS\n");
    printf("%s\n", LINE);

    for (int i = 0; i < n; i++) {
        /* Semua proses baru pada MLFQ masuk melalui Q0. */
        printf("P%d : Q0 (t=%d)",
               processes[i].pid, processes[i].arrival_time);

        for (int j = 0; j < migration_log_count; j++) {
            if (migration_logs[j].pid == processes[i].pid) {
                printf(" -> Q%d (t=%d)",
                       migration_logs[j].to_queue,
                       migration_logs[j].time);
            }
        }

        if (processes[i].state == TERMINATED) {
            printf(" -> TERMINATED (t=%d)",
                   processes[i].completion_time);
        }

        printf("\n");
    }

    printf("%s\n\n", LINE);
}

/* Output Khusus MLFQ: tampilkan detail setiap perpindahan queue. */
void print_queue_migrations(void) {
    printf("%s\n", LINE);
    printf("QUEUE MIGRATIONS\n");
    printf("%s\n", LINE);

    if (migration_log_count == 0) {
        printf("Tidak ada perpindahan queue.\n");
    }

    for (int i = 0; i < migration_log_count; i++) {
        printf("t=%d : P%d Q%d -> Q%d "
               "(quantum Q%d habis; sisa BT=%d)\n",
               migration_logs[i].time,
               migration_logs[i].pid,
               migration_logs[i].from_queue,
               migration_logs[i].to_queue,
               migration_logs[i].from_queue,
               migration_logs[i].remaining_time);
    }

    printf("Total Queue Migration : %d\n", migration_log_count);
    printf("%s\n\n", LINE);
}

/* Output Khusus MLFQ: tampilkan preemption antarqueue. */
void print_higher_queue_preemptions(void) {
    printf("%s\n", LINE);
    printf("HIGHER-QUEUE PREEMPTIONS\n");
    printf("%s\n", LINE);

    if (preemption_log_count == 0) {
        printf("Tidak ada preemption antarqueue.\n");
    }

    for (int i = 0; i < preemption_log_count; i++) {
        printf("t=%d : P%d(Q%d) PREEMPTED (sisa BT=%d) "
               "-> P%d(Q%d) RUNNING\n",
               preemption_logs[i].time,
               preemption_logs[i].stopped_pid,
               preemption_logs[i].stopped_queue,
               preemption_logs[i].stopped_remaining_time,
               preemption_logs[i].incoming_pid,
               preemption_logs[i].incoming_queue);
    }

    printf("Total Preemption Antarqueue : %d\n",
           preemption_log_count);
    printf("%s\n\n", LINE);
}

/* ============================================================
 * BAGIAN 5 - READY QUEUE (LINKED LIST), MENU INPUT + 5 SKENARIO,
 *            PRIORITY BOOST (OPSIONAL)
 * ============================================================ */

#define NUM_QUEUES    3     /* Q0 (RR), Q1 (RR), Q2 (FCFS) */
#define MAX_PROCESS   20
#define MAX_ARRIVAL   1000
#define MAX_BURST     100
#define MAX_QUANTUM   50
#define MIN_BOOST     10    /* interval boost minimal, agar log boost cukup */
#define MAX_BOOST_LOG 4000

/* ---------------- READY QUEUE (LINKED LIST) ---------------- */

/* Satu node linked list. Node hanya MENUNJUK ke PCB asli (bukan salinan),
 * jadi perubahan remaining_time dll. langsung terlihat di array proses. */
typedef struct QueueNode {
    PCB *process;
    struct QueueNode *next;
} QueueNode;

/* Satu ready queue. head = proses yang dapat CPU berikutnya,
 * tail = tempat proses baru masuk. Menyimpan tail membuat enqueue O(1). */
typedef struct {
    QueueNode *head;
    QueueNode *tail;
    int level;    /* 0 = prioritas tertinggi */
    int quantum;  /* 0 berarti FCFS (tidak ada quantum) */
} ReadyQueue;

/* Konfigurasi MLFQ yang diisi lewat menu input. */
typedef struct {
    int quantum[NUM_QUEUES];  /* quantum[2] selalu 0 karena Q2 = FCFS */
    int boost_interval;       /* 0 = priority boost tidak aktif */
} MLFQConfig;

int queue_is_empty(const ReadyQueue *q) {
    return q->head == NULL;
}

/* Masukkan proses ke EKOR queue. queue_level di PCB ikut diperbarui
 * supaya selalu sama dengan queue tempat proses berada. */
void queue_enqueue(ReadyQueue *q, PCB *process) {
    QueueNode *node = malloc(sizeof *node);
    if (node == NULL) {
        fprintf(stderr, "Gagal alokasi memori untuk node queue.\n");
        exit(EXIT_FAILURE);
    }
    node->process = process;
    node->next = NULL;

    if (q->tail == NULL) {        /* queue kosong: node jadi head & tail */
        q->head = node;
        q->tail = node;
    } else {                      /* sambungkan di belakang tail lama */
        q->tail->next = node;
        q->tail = node;
    }
    process->queue_level = q->level;
}

/* Ambil proses dari KEPALA queue (FIFO). Node di-free, PCB tetap ada.
 * Mengembalikan NULL jika queue kosong. */
PCB *queue_dequeue(ReadyQueue *q) {
    if (q->head == NULL) {
        return NULL;
    }
    QueueNode *node = q->head;
    PCB *process = node->process;

    q->head = node->next;
    if (q->head == NULL) {        /* queue jadi kosong: tail juga di-reset */
        q->tail = NULL;
    }
    free(node);
    return process;
}

/* Lihat proses terdepan tanpa mengeluarkannya
 * (dipakai untuk mencatat proses yang mem-preempt). */
PCB *queue_peek(const ReadyQueue *q) {
    return q->head == NULL ? NULL : q->head->process;
}

/* Siapkan Q0, Q1, Q2 kosong sesuai konfigurasi. */
void mlfq_init_queues(ReadyQueue queues[], const MLFQConfig *cfg) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        queues[i].head = NULL;
        queues[i].tail = NULL;
        queues[i].level = i;
        queues[i].quantum = cfg->quantum[i];
    }
}

/* Level queue tertinggi yang punya proses READY, atau -1 jika semua kosong.
 * Dipakai scheduler untuk (1) memilih proses berikutnya dan
 * (2) mengecek preemption: jika hasilnya < level proses yang sedang jalan. */
int mlfq_highest_ready(const ReadyQueue queues[]) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        if (!queue_is_empty(&queues[i])) {
            return i;
        }
    }
    return -1;
}

/* Jadikan proses READY di queue tertentu sekaligus catat ke log Bagian 7.
 * Dipakai saat: proses datang (level 0), quantum habis (level + 1),
 * dipreempt queue lebih tinggi (level sama), dan priority boost (level 0). */
void mlfq_make_ready(ReadyQueue queues[], PCB *process, int level, int time) {
    process->state = READY;
    queue_enqueue(&queues[level], process);
    record_process_state(process, time);
}

/* Bebaskan semua node yang masih tersisa (panggil di akhir simulasi). */
void mlfq_free_queues(ReadyQueue queues[]) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        while (queue_dequeue(&queues[i]) != NULL) {
        }
    }
}

/* ---------------- PRIORITY BOOST (OPSIONAL) ----------------
 * Setiap boost_interval time unit, semua proses di Q1 dan Q2 dinaikkan
 * ke Q0. Tujuannya mencegah starvation: tanpa boost, proses di Q2 bisa
 * terus tertunda jika proses baru terus berdatangan ke Q0. */

/* Satu baris riwayat boost: proses pid naik dari from_queue ke Q0. */
typedef struct {
    int time;
    int pid;
    int from_queue;
} BoostLog;

static BoostLog boost_logs[MAX_BOOST_LOG];
static int boost_log_count = 0;

/* Kosongkan riwayat boost sebelum satu simulasi dimulai. */
void reset_boost_logs(void) {
    boost_log_count = 0;
}

static void record_boost(const PCB *process, int from_queue, int time) {
    if (boost_log_count >= MAX_BOOST_LOG) {
        return;
    }
    boost_logs[boost_log_count].time = time;
    boost_logs[boost_log_count].pid = process->pid;
    boost_logs[boost_log_count].from_queue = from_queue;
    boost_log_count++;
}

/* Apakah boost harus dilakukan pada waktu ini? (t=0 tidak dihitung) */
int boost_due(const MLFQConfig *cfg, int time) {
    return cfg->boost_interval > 0 && time > 0 &&
           time % cfg->boost_interval == 0;
}

/* Naikkan semua proses Q1 lalu Q2 ke ekor Q0 (urutan di dalam queue tetap).
 * Jika proses yang sedang RUNNING berada di Q1/Q2, level-nya juga jadi 0.
 *
 * Return 1 jika proses RUNNING ikut di-boost. Dalam kasus itu scheduler
 * harus mereset hitungan quantum proses tsb dan memotong slice Gantt
 * (log_add_slice) di waktu ini karena level queue-nya berubah. */
int priority_boost(ReadyQueue queues[], PCB *running, int time) {
    for (int level = 1; level < NUM_QUEUES; level++) {
        PCB *p;
        while ((p = queue_dequeue(&queues[level])) != NULL) {
            record_boost(p, level, time);
            mlfq_make_ready(queues, p, 0, time);
        }
    }

    if (running != NULL && running->queue_level > 0) {
        record_boost(running, running->queue_level, time);
        running->queue_level = 0;
        record_process_state(running, time);  /* tercatat RUNNING Q0 */
        return 1;
    }
    return 0;
}

/* Output priority boost, sesuai format contoh di soal. */
void print_priority_boosts(const MLFQConfig *cfg) {
    printf("%s\n", LINE);
    printf("PRIORITY BOOST (setiap %d time unit)\n", cfg->boost_interval);
    printf("%s\n", LINE);

    for (int i = 0; i < boost_log_count; i++) {
        /* kelompokkan baris berdasarkan waktu boost */
        if (i == 0 || boost_logs[i].time != boost_logs[i - 1].time) {
            printf("Priority Boost at t = %d\n", boost_logs[i].time);
        }
        printf("P%d : Q%d -> Q0\n", boost_logs[i].pid,
               boost_logs[i].from_queue);
    }
    printf("%s\n\n", LINE);
}

/* ---------------- MENU INPUT + 5 SKENARIO ----------------
 * Input dibaca dengan scanf. Yang dicek hanya batas nilai yang bisa
 * membuat simulasi error:
 *   - jumlah proses <= MAX_PROCESS : array processes tidak overflow
 *   - AT >= 0                      : proses tidak pernah "datang" jika AT < 0
 *   - BT >= 1                      : BT 0 membuat proses tidak pernah selesai
 *   - quantum >= 1                 : quantum 0 berarti FCFS di program ini
 * Jika scanf gagal membaca angka, program berhenti. */

/* Baca satu bilangan bulat dalam rentang [min, max], ulangi jika di luar. */
static int read_int_range(const char *prompt, int min, int max) {
    int value;
    while (1) {
        printf("%s", prompt);
        if (scanf("%d", &value) != 1) {
            printf("\nInput harus berupa angka. Program berhenti.\n");
            exit(EXIT_FAILURE);
        }
        if (value >= min && value <= max) {
            return value;
        }
        printf("  Nilai harus di antara %d dan %d.\n", min, max);
    }
}

/* Baca jawaban yes/no Spasi sebelum %c melewati Enter sisa input sebelumnya. */
static int read_yes_no(const char *prompt) {
    char answer;
    printf("%s", prompt);
    if (scanf(" %c", &answer) != 1) {
        printf("\nInput berakhir. Program berhenti.\n");
        exit(EXIT_FAILURE);
    }
    return answer == 'y' || answer == 'Y';
}

/* Isi PCB dengan nilai awal. first_start_time = -1 artinya
 * proses belum pernah mendapat CPU (dipakai untuk Response Time). */
static void init_pcb(PCB *p, int pid, int arrival, int burst) {
    memset(p, 0, sizeof *p);
    p->pid = pid;
    p->arrival_time = arrival;
    p->burst_time = burst;
    p->remaining_time = burst;
    p->queue_level = 0;         /* semua proses baru masuk Q0 */
    p->state = NEW;
    p->first_start_time = -1;
}

/* Data skenario yang di-hardcode. Semua data dibuat sendiri. */
typedef struct {
    const char *name;
    int n;
    int arrival[MAX_PROCESS];
    int burst[MAX_PROCESS];
    int quantum0;
    int quantum1;
    int boost_interval;  /* 0 = mati */
} TestScenario;

static const TestScenario SCENARIOS[] = {
    {   /* 5 proses, AT berdekatan dan BT tidak ekstrem */
        "Skenario 1 - Kondisi Normal", 5,
        {0, 1, 2, 4, 5},
        {6, 4, 7, 3, 5},
        2, 4, 0
    },
    {   /* proses datang tersebar: ada CPU idle dan preemption antarqueue */
        "Skenario 2 - Arrival Time Berbeda", 5,
        {0, 5, 6, 11, 20},
        {3, 6, 2, 4, 3},
        2, 4, 0
    },
    {   /* BT sangat besar dan sangat kecil dalam satu set; boost tiap 20 */
        "Skenario 3 - Burst Time Berbeda Signifikan", 6,
        {0, 1, 2, 3, 5, 12},
        {30, 2, 1, 18, 3, 1},
        2, 4, 20
    },
    {   /* 12 proses sekaligus */
        "Skenario 4 - Banyak Proses", 12,
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
        {5, 3, 8, 2, 6, 4, 1, 7, 3, 5, 2, 9},
        2, 4, 0
    },
    {   /* AT semua 0; BT sama (P2,P3,P5); BT = quantum Q0 (P1);
         * BT = Q0+Q1 (P2,P3,P5); BT sangat kecil (P4) & sangat besar (P6) */
        "Skenario 5 - Edge Case", 6,
        {0, 0, 0, 0, 0, 0},
        {2, 6, 6, 1, 6, 40},
        2, 4, 0
    }
};
#define NUM_SCENARIOS ((int)(sizeof SCENARIOS / sizeof SCENARIOS[0]))

/* Input manual: jumlah proses, quantum, boost, lalu AT dan BT tiap proses. */
static int input_manual(PCB processes[], MLFQConfig *cfg) {
    int n = read_int_range("Jumlah proses: ", 1, MAX_PROCESS);

    cfg->quantum[0] = read_int_range("Quantum Q0 (RR, > 0): ", 1, MAX_QUANTUM);
    cfg->quantum[1] = read_int_range("Quantum Q1 (RR, > 0): ", 1, MAX_QUANTUM);
    cfg->quantum[2] = 0;  /* Q2 = FCFS */

    if (read_yes_no("Aktifkan priority boost? (y/n): ")) {
        cfg->boost_interval = read_int_range(
            "Boost setiap berapa time unit (>= 10): ", MIN_BOOST, 1000);
    } else {
        cfg->boost_interval = 0;
    }

    for (int i = 0; i < n; i++) {
        int at, bt;
        while (1) {
            printf("P%d - masukkan Arrival Time dan Burst Time: ", i + 1);
            if (scanf("%d %d", &at, &bt) != 2) {
                printf("\nInput harus berupa angka. Program berhenti.\n");
                exit(EXIT_FAILURE);
            }
            if (at >= 0 && at <= MAX_ARRIVAL && bt >= 1 && bt <= MAX_BURST) {
                break;
            }
            printf("  AT harus 0..%d dan BT harus 1..%d.\n",
                   MAX_ARRIVAL, MAX_BURST);
        }
        init_pcb(&processes[i], i + 1, at, bt);
    }
    return n;
}

/* Muat data proses dan konfigurasi dari skenario yang dipilih. */
static int load_scenario(const TestScenario *s, PCB processes[],
                         MLFQConfig *cfg) {
    printf("\n>> %s\n", s->name);
    for (int i = 0; i < s->n; i++) {
        init_pcb(&processes[i], i + 1, s->arrival[i], s->burst[i]);
    }
    cfg->quantum[0] = s->quantum0;
    cfg->quantum[1] = s->quantum1;
    cfg->quantum[2] = 0;
    cfg->boost_interval = s->boost_interval;
    return s->n;
}

/* Menu utama. Mengisi processes[] dan cfg.
 * Return jumlah proses, atau 0 jika user memilih keluar. */
int input_menu(PCB processes[], MLFQConfig *cfg) {

    printf("\n%s\n", LINE);
    printf("MULTILEVEL FEEDBACK QUEUE SCHEDULER\n");
    printf("%s\n", LINE);
    printf("1. Input manual\n");
    for (int i = 0; i < NUM_SCENARIOS; i++) {
        printf("%d. %s\n", i + 2, SCENARIOS[i].name);
    }
    printf("0. Keluar\n");

    int choice = read_int_range("Pilih menu: ", 0, NUM_SCENARIOS + 1);
    if (choice == 0) {
        return 0;
    }
    if (choice == 1) {
        return input_manual(processes, cfg);
    }
    return load_scenario(&SCENARIOS[choice - 2], processes, cfg);
}

/* BAGIAN 1 - PROCESS INPUT versi MLFQ: tampilkan input dan konfigurasi queue. */
void print_process_input(const PCB processes[], int n, const MLFQConfig *cfg) {
    printf("\n%s\n", LINE);
    printf("PROCESS INPUT AND QUEUE CONFIGURATION\n");
    printf("%s\n", LINE);
    printf("Q0: RR (quantum=%d) | Q1: RR (quantum=%d) | Q2: FCFS\n",
           cfg->quantum[0], cfg->quantum[1]);
    if (cfg->boost_interval > 0) {
        printf("Priority Boost : setiap %d time unit\n", cfg->boost_interval);
    } else {
        printf("Priority Boost : tidak aktif\n");
    }
    printf("%-10s%10s%10s%16s\n", "PID", "AT", "BT", "Initial Queue");
    printf("%s\n", DASH);
    for (int i = 0; i < n; i++) {
        printf("P%-9d%10d%10d%16s\n", processes[i].pid,
               processes[i].arrival_time, processes[i].burst_time, "Q0");
    }
    printf("%s\n\n", LINE);
}

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

#define MAX_SLICES 2500   /* slice >= 1 time unit, total CPU time <= 20 x 100 */
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

    /* FIX IDLE: jika ada celah antara akhir slice sebelumnya dan awal
     * slice berikutnya, berarti CPU idle (belum ada proses yang datang).
     * Celah itu dicetak sebagai kotak IDLE beserta waktu mulainya. */
    int prev_end = 0;

    printf("|");
    for (int i = 0; i < log->count; i++) {
        if (log->slices[i].start_time > prev_end) {
            printf(" IDLE|");                                   /* FIX IDLE */
        }
        printf(" P%-3d|", log->slices[i].pid);
        prev_end = log->slices[i].end_time;
    }
    printf("\n");

    prev_end = 0;
    printf("0");                                                /* FIX IDLE */
    for (int i = 0; i < log->count; i++) {
        if (log->slices[i].start_time > prev_end) {
            printf("%*d", 6, log->slices[i].start_time);        /* FIX IDLE */
        }
        printf("%*d", 6, log->slices[i].end_time);
        prev_end = log->slices[i].end_time;
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

 int main(void) {
    PCB processes[MAX_PROCESS];
    MLFQConfig cfg;
    ReadyQueue queues[NUM_QUEUES];
    ExecutionLog log;

    while (1) {
        int n = input_menu(processes, &cfg);
        if (n == 0) break;

        print_process_input(processes, n, &cfg);
        mlfq_init_queues(queues, &cfg);
        reset_mlfq_logs();
        reset_boost_logs();
        log_init(&log);

        int time = 0, done = 0;
        PCB *running = NULL;
        int slice_start = 0, quantum_used = 0;

        while (done < n) {
            /* 1. Priority boost (opsional) */
            if (boost_due(&cfg, time)) {
                int old_level = running ? running->queue_level : 0;
                if (priority_boost(queues, running, time)) {
                    log_add_slice(&log, running->pid, old_level, slice_start, time);
                    slice_start = time;
                    quantum_used = 0;
                }
            }

            /* 2. Proses yang tiba pada waktu ini masuk ekor Q0 */
            for (int i = 0; i < n; i++) {
                if (processes[i].state == NEW && processes[i].arrival_time <= time) {
                    mlfq_make_ready(queues, &processes[i], 0, time);
                }
            }

            /* 3. Preemption oleh queue yang lebih tinggi */
            int hi = mlfq_highest_ready(queues);
            if (running != NULL && hi != -1 && hi < running->queue_level) {
                record_higher_queue_preemption(running, queue_peek(&queues[hi]), time);
                log_add_slice(&log, running->pid, running->queue_level, slice_start, time);
                mlfq_make_ready(queues, running, running->queue_level, time);
                running = NULL;
            }

            /* 4. Dispatch: ambil kepala queue tertinggi yang tidak kosong */
            if (running == NULL) {
                if (hi == -1) {          /* CPU idle */
                    time++;
                    continue;
                }
                running = queue_dequeue(&queues[hi]);
                running->state = RUNNING;
                if (running->first_start_time == -1) running->first_start_time = time;
                record_process_state(running, time);
                slice_start = time;
                quantum_used = 0;
            }

            /* 5. Jalankan 1 time unit */
            running->remaining_time--;
            quantum_used++;
            time++;

            /* 6. Selesai atau quantum habis? */
            if (running->remaining_time == 0) {
                running->state = TERMINATED;
                running->completion_time = time;
                record_process_state(running, time);
                log_add_slice(&log, running->pid, running->queue_level, slice_start, time);
                done++;
                running = NULL;
            } else if (running->queue_level < NUM_QUEUES - 1 &&
                       quantum_used >= cfg.quantum[running->queue_level]) {
                int old_level = running->queue_level;
                log_add_slice(&log, running->pid, old_level, slice_start, time);
                record_queue_migration(running, old_level, old_level + 1, time);
                running->queue_level = old_level + 1;
                mlfq_make_ready(queues, running, running->queue_level, time);
                running = NULL;
            }
        }

        /* Output (urutan sama seperti contoh soal) */
        hitung_metrics(processes, n);
        print_gantt_chart(&log);
        print_process_queue_movements(processes, n);
        print_queue_migrations();
        if (cfg.boost_interval > 0) print_priority_boosts(&cfg);
        print_higher_queue_preemptions();
        print_scheduling_table(processes, n);
        print_averages(processes, n);
        print_util_throughput(processes, n);
        print_context_switch_info(&log);
        print_process_state_transitions(processes, n);

        mlfq_free_queues(queues);
    }

    printf("Program selesai.\n");
    return 0;
}