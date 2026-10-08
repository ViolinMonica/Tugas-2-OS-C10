/* ======================================================================
 * scheduler.c
 * Tugas 2 Sistem Operasi - Multilevel Feedback Queue (MLFQ) Scheduler
 * Fakultas Ilmu Komputer, Universitas Indonesia - Gasal 2026/2027
 *
 * Kelompok : <KELAS><KELOMPOK>
 * Anggota  : <isi nama & bagian masing-masing>
 *
 * Konfigurasi MLFQ:
 *   Q0 : Round Robin (quantum dari input)  - prioritas tertinggi
 *   Q1 : Round Robin (quantum dari input)
 *   Q2 : FCFS                              - prioritas terendah
 *
 * Aturan:
 *   1. Proses baru selalu masuk ke ekor Q0.
 *   2. CPU selalu diberikan ke kepala queue tertinggi yang tidak kosong.
 *   3. Jika quantum habis dan proses belum selesai -> turun satu level
 *      (Q0 -> Q1 -> Q2). Q2 FCFS tidak punya quantum.
 *   4. Jika ada proses READY di queue yang lebih tinggi daripada proses
 *      yang sedang RUNNING, proses tsb di-preempt dan kembali ke ekor
 *      queue-nya sendiri (level tidak turun, hitungan quantum di-reset).
 *   5. (Opsional) Priority boost: tiap S time unit semua proses di Q1/Q2
 *      dinaikkan ke Q0 untuk mencegah starvation.
 *
 * Simulasi berjalan per 1 time unit (tick). Tidak ada simulasi I/O,
 * sehingga state BLOCKED didefinisikan tetapi tidak pernah dipakai.
 *
 * Kompilasi : gcc -std=c99 -Wall -o scheduler scheduler.c
 * ====================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE "======================================================================="
#define DASH "-----------------------------------------------------------------------"

#define NUM_QUEUES    3     /* Q0 (RR), Q1 (RR), Q2 (FCFS) */
#define MAX_PROCESS   20
#define MAX_ARRIVAL   1000
#define MAX_BURST     100
#define MAX_QUANTUM   50
#define MIN_BOOST     10    /* boost minimal tiap 10 time unit agar log tidak meledak */

/* ======================================================================
 * PCB (Process Control Block)
 * ====================================================================== */

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

/* ======================================================================
 * BAGIAN 7 + OUTPUT KHUSUS MLFQ (state log, migrasi, preemption)
 * ====================================================================== */

/* Kapasitas log dihitung dari batas input (20 proses, BT <= 100, AT <= 1000,
 * boost minimal tiap 10). Kasus terberat (q=1, boost tiap 10) butuh sekitar
 * 6000 state log, 4000 migrasi, dan 2000 slice. Nilai di bawah memberi
 * cadangan agar log tidak pernah penuh untuk input yang lolos validasi. */
#define MAX_STATE_LOG 12000
#define MAX_MIGRATION_LOG 8000
#define MAX_PREEMPTION_LOG 500

/* Struct berikut BUKAN pengganti PCB. PCB hanya menyimpan state dan queue
 * proses saat ini; kalau state berubah, nilai lama tertimpa. Karena itu
 * riwayat disimpan di log terpisah. */

/* Menyimpan satu perubahan state milik sebuah proses. */
typedef struct {
    int pid;
    ProcessState state;
    int time;
    int queue_level;
} StateLog;

/* Menyimpan perpindahan proses dari satu queue ke queue lain.
 * is_boost = 0 : turun karena quantum habis
 * is_boost = 1 : naik ke Q0 karena priority boost */
typedef struct {
    int pid;
    int from_queue;
    int to_queue;
    int time;
    int remaining_time;
    int is_boost;
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

/* Catat state terbaru proses ke history (dipanggil SETELAH field state
 * di PCB diperbarui): datang, dapat CPU, dipreempt, quantum habis, selesai. */
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

static void add_migration(const PCB *process, int source_queue,
                          int destination_queue, int time, int is_boost) {
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
    migration_logs[migration_log_count].is_boost = is_boost;
    migration_log_count++;
}

/* Catat proses turun queue karena quantum Q0/Q1 habis. */
void record_queue_migration(const PCB *process,
                            int source_queue,
                            int destination_queue,
                            int time) {
    add_migration(process, source_queue, destination_queue, time, 0);
}

/* Catat proses naik ke Q0 karena priority boost. */
void record_boost_migration(const PCB *process, int source_queue, int time) {
    add_migration(process, source_queue, 0, time, 1);
}

/* Catat jika proses pada queue rendah dihentikan karena ada proses READY
 * pada queue yang lebih tinggi. Preemption tidak mengubah queue_level. */
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
                printf(" -> Q%d (t=%d%s)",
                       migration_logs[j].to_queue,
                       migration_logs[j].time,
                       migration_logs[j].is_boost ? ", boost" : "");
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
    int demotions = 0, boosts = 0;

    printf("%s\n", LINE);
    printf("QUEUE MIGRATIONS\n");
    printf("%s\n", LINE);

    if (migration_log_count == 0) {
        printf("Tidak ada perpindahan queue.\n");
    }

    for (int i = 0; i < migration_log_count; i++) {
        if (migration_logs[i].is_boost) {
            printf("t=%d : P%d Q%d -> Q%d (priority boost; sisa BT=%d)\n",
                   migration_logs[i].time,
                   migration_logs[i].pid,
                   migration_logs[i].from_queue,
                   migration_logs[i].to_queue,
                   migration_logs[i].remaining_time);
            boosts++;
        } else {
            printf("t=%d : P%d Q%d -> Q%d "
                   "(quantum Q%d habis; sisa BT=%d)\n",
                   migration_logs[i].time,
                   migration_logs[i].pid,
                   migration_logs[i].from_queue,
                   migration_logs[i].to_queue,
                   migration_logs[i].from_queue,
                   migration_logs[i].remaining_time);
            demotions++;
        }
    }

    printf("Total Queue Migration : %d", migration_log_count);
    if (boosts > 0) {
        printf(" (turun: %d, boost: %d)", demotions, boosts);
    }
    printf("\n%s\n\n", LINE);
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
 * BAGIAN 5A - READY QUEUE (LINKED LIST)
 * ============================================================ */

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
    int count;
    int level;    /* 0 = prioritas tertinggi */
    int quantum;  /* 0 berarti FCFS (tidak ada quantum) */
} ReadyQueue;

/* Konfigurasi MLFQ yang diisi lewat menu input. */
typedef struct {
    int quantum[NUM_QUEUES];  /* quantum[2] selalu 0 karena Q2 = FCFS */
    int boost_interval;       /* 0 = priority boost tidak aktif */
} MLFQConfig;

void queue_init(ReadyQueue *q, int level, int quantum) {
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    q->level = level;
    q->quantum = quantum;
}

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
    q->count++;
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
    q->count--;
    return process;
}

/* Lihat proses terdepan tanpa mengeluarkannya. */
PCB *queue_peek(const ReadyQueue *q) {
    return q->head == NULL ? NULL : q->head->process;
}

/* Kosongkan queue dan bebaskan semua node. */
void queue_clear(ReadyQueue *q) {
    while (queue_dequeue(q) != NULL) {
    }
}

/* Cetak isi queue, contoh: Q1 [RR q=3]: P1(sisa=6) -> P2(sisa=2) -> NULL */
void queue_print(const ReadyQueue *q) {
    if (q->quantum > 0) {
        printf("Q%d [RR q=%d]: ", q->level, q->quantum);
    } else {
        printf("Q%d [FCFS]  : ", q->level);
    }
    for (const QueueNode *n = q->head; n != NULL; n = n->next) {
        printf("P%d(sisa=%d) -> ", n->process->pid,
               n->process->remaining_time);
    }
    printf("NULL\n");
}

/* Siapkan Q0, Q1, Q2 sesuai konfigurasi. */
void mlfq_init_queues(ReadyQueue queues[], const MLFQConfig *cfg) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        queue_init(&queues[i], i, cfg->quantum[i]);
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

/* Snapshot isi ketiga queue, berguna untuk trace saat demo. */
void mlfq_print_queues(const ReadyQueue queues[], int time) {
    printf("  [t=%d] isi ready queue:\n", time);
    for (int i = 0; i < NUM_QUEUES; i++) {
        printf("    ");
        queue_print(&queues[i]);
    }
}

/* Bebaskan semua node yang masih tersisa (panggil di akhir simulasi). */
void mlfq_free_queues(ReadyQueue queues[]) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        queue_clear(&queues[i]);
    }
}

/* ============================================================
 * BAGIAN 5C - PRIORITY BOOST (OPSIONAL)
 * Setiap boost_interval time unit, semua proses di Q1 dan Q2 dinaikkan
 * ke Q0. Tujuannya mencegah starvation: tanpa boost, proses di Q2 bisa
 * terus tertunda jika proses baru terus berdatangan ke Q0.
 * ============================================================ */

static int boost_trigger_count = 0;

void reset_boost_logs(void) {
    boost_trigger_count = 0;
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
 * WAJIB: reset penghitung quantum proses tsb, dan memotong slice Gantt
 * (log_add_slice) di waktu ini karena level queue-nya berubah. */
int priority_boost(ReadyQueue queues[], PCB *running, int time) {
    boost_trigger_count++;

    for (int level = 1; level < NUM_QUEUES; level++) {
        PCB *p;
        while ((p = queue_dequeue(&queues[level])) != NULL) {
            record_boost_migration(p, level, time);
            mlfq_make_ready(queues, p, 0, time);
        }
    }

    if (running != NULL && running->queue_level > 0) {
        record_boost_migration(running, running->queue_level, time);
        running->queue_level = 0;
        record_process_state(running, time);  /* tercatat RUNNING Q0 */
        return 1;
    }
    return 0;
}

/* Output tambahan (opsional di soal): riwayat priority boost. */
void print_priority_boosts(const MLFQConfig *cfg) {
    printf("%s\n", LINE);
    printf("PRIORITY BOOST\n");
    printf("%s\n", LINE);

    if (cfg->boost_interval <= 0) {
        printf("Priority boost tidak diaktifkan.\n");
        printf("%s\n\n", LINE);
        return;
    }

    printf("Boost interval : setiap %d time unit\n\n", cfg->boost_interval);

    int moved = 0, last_time = -1;
    for (int i = 0; i < migration_log_count; i++) {
        if (!migration_logs[i].is_boost) {
            continue;
        }
        /* kelompokkan baris berdasarkan waktu boost */
        if (migration_logs[i].time != last_time) {
            printf("Priority Boost at t = %d\n", migration_logs[i].time);
            last_time = migration_logs[i].time;
        }
        printf("  P%d : Q%d -> Q0\n", migration_logs[i].pid,
               migration_logs[i].from_queue);
        moved++;
    }
    if (moved == 0) {
        printf("Tidak ada proses di Q1/Q2 saat boost terjadi.\n");
    }
    printf("\nBoost dipicu sebanyak : %d kali\n", boost_trigger_count);
    printf("Total proses di-boost : %d\n", moved);
    printf("%s\n\n", LINE);
}

/* ============================================================
 * BAGIAN 5B - MENU INPUT + 5 SKENARIO TES
 * ============================================================ */

/* Baca satu baris. Program berhenti dengan rapi jika input habis (EOF). */
static void read_line(char *buf, size_t size) {
    if (fgets(buf, (int)size, stdin) == NULL) {
        printf("\nInput berakhir. Program selesai.\n");
        exit(EXIT_SUCCESS);
    }
    /* Jika baris lebih panjang dari buffer, buang sisanya supaya tidak
     * terbaca sebagai jawaban untuk pertanyaan berikutnya. */
    if (strchr(buf, '\n') == NULL) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {
        }
    }
}

/* Baca satu bilangan bulat dalam rentang [min, max], ulangi jika salah. */
static int read_int_range(const char *prompt, int min, int max) {
    char buf[128];
    int value;
    char extra;
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        read_line(buf, sizeof buf);
        if (sscanf(buf, "%d %c", &value, &extra) == 1 &&
            value >= min && value <= max) {
            return value;
        }
        printf("  Input tidak valid. Masukkan bilangan bulat %d..%d.\n",
               min, max);
    }
}

/* Baca Arrival Time dan Burst Time dalam satu baris, contoh: "0 8". */
static void read_at_bt(int pid, int *at, int *bt) {
    char buf[128];
    char extra;
    while (1) {
        printf("P%d - masukkan Arrival Time dan Burst Time: ", pid);
        fflush(stdout);
        read_line(buf, sizeof buf);
        if (sscanf(buf, "%d %d %c", at, bt, &extra) == 2 &&
            *at >= 0 && *at <= MAX_ARRIVAL &&
            *bt >= 1 && *bt <= MAX_BURST) {
            return;
        }
        printf("  Input tidak valid. AT: 0..%d, BT: 1..%d (pisahkan spasi).\n",
               MAX_ARRIVAL, MAX_BURST);
    }
}

static int read_yes_no(const char *prompt) {
    char buf[128];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        read_line(buf, sizeof buf);
        if (buf[0] == 'y' || buf[0] == 'Y') return 1;
        if (buf[0] == 'n' || buf[0] == 'N') return 0;
        printf("  Jawab dengan y atau n.\n");
    }
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
    p->completion_time = -1;
}

static void read_config(MLFQConfig *cfg) {
    cfg->quantum[0] = read_int_range("Quantum Q0 (RR, > 0): ", 1, MAX_QUANTUM);
    cfg->quantum[1] = read_int_range("Quantum Q1 (RR, > 0): ", 1, MAX_QUANTUM);
    cfg->quantum[2] = 0;  /* Q2 = FCFS */
    if (read_yes_no("Aktifkan priority boost? (y/n): ")) {
        cfg->boost_interval =
            read_int_range("Boost setiap berapa time unit (>= 10): ", MIN_BOOST, 1000);
    } else {
        cfg->boost_interval = 0;
    }
}

/* Data skenario yang di-hardcode. Semua data dibuat sendiri. */
typedef struct {
    const char *name;
    const char *purpose;
    int n;
    int arrival[MAX_PROCESS];
    int burst[MAX_PROCESS];
    int quantum0;
    int quantum1;
    int boost_interval;  /* 0 = mati */
} TestScenario;

static const TestScenario SCENARIOS[] = {
    {
        "Skenario 1 - Kondisi Normal",
        "5 proses, AT berdekatan dan BT tidak ekstrem.",
        5,
        {0, 1, 2, 4, 5},
        {6, 4, 7, 3, 5},
        2, 4, 0
    },
    {
        "Skenario 2 - Arrival Time Berbeda (ada CPU idle)",
        "Proses datang tersebar; ada CPU idle dan preemption antarqueue.",
        5,
        {0, 5, 6, 11, 20},
        {3, 6, 2, 4, 3},
        2, 4, 0
    },
    {
        "Skenario 3 - Burst Time Berbeda Signifikan (+ priority boost)",
        "BT sangat besar dan sangat kecil dalam satu set; boost tiap 20.",
        6,
        {0, 1, 2, 3, 5, 12},
        {30, 2, 1, 18, 3, 1},
        2, 4, 20
    },
    {
        "Skenario 4 - Banyak Proses",
        "12 proses sekaligus untuk menguji kestabilan linked list.",
        12,
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
        {5, 3, 8, 2, 6, 4, 1, 7, 3, 5, 2, 9},
        2, 4, 0
    },
    {
        "Skenario 5 - Edge Case",
        "AT semua 0; BT sama (P2,P3,P5); BT = quantum Q0 (P1); "
        "BT = Q0+Q1 (P2,P3,P5); BT sangat kecil (P4) & sangat besar (P6).",
        6,
        {0, 0, 0, 0, 0, 0},
        {2, 6, 6, 1, 6, 40},
        2, 4, 0
    }
};
#define NUM_SCENARIOS ((int)(sizeof SCENARIOS / sizeof SCENARIOS[0]))

/* Contoh dari dokumen soal, hanya untuk mencocokkan output program. */
static const TestScenario EXAMPLE_FROM_DOC = {
    "Contoh dari dokumen soal (verifikasi)",
    "Hasil harus sama dengan contoh output MLFQ di soal.",
    4,
    {0, 1, 2, 3},
    {8, 4, 2, 5},
    2, 3, 0
};

static int load_scenario(const TestScenario *s, PCB processes[],
                         MLFQConfig *cfg) {
    printf("\n>> %s\n   %s\n", s->name, s->purpose);
    for (int i = 0; i < s->n; i++) {
        init_pcb(&processes[i], i + 1, s->arrival[i], s->burst[i]);
    }
    cfg->quantum[0] = s->quantum0;
    cfg->quantum[1] = s->quantum1;
    cfg->quantum[2] = 0;
    cfg->boost_interval = s->boost_interval;

    printf("   Konfigurasi default: Q0 q=%d, Q1 q=%d, boost=",
           cfg->quantum[0], cfg->quantum[1]);
    if (cfg->boost_interval > 0) {
        printf("tiap %d\n", cfg->boost_interval);
    } else {
        printf("mati\n");
    }
    if (!read_yes_no("Pakai konfigurasi default? (y/n): ")) {
        read_config(cfg);
    }
    return s->n;
}

/* Menu utama. Mengisi processes[] dan cfg.
 * Return jumlah proses, atau 0 jika user memilih keluar.
 * *trace = 1 jika user ingin melihat isi queue setiap dispatch. */
int input_menu(PCB processes[], MLFQConfig *cfg, int *trace) {
    int n;

    printf("\n%s\n", LINE);
    printf("MULTILEVEL FEEDBACK QUEUE SCHEDULER\n");
    printf("Q0: Round Robin | Q1: Round Robin | Q2: FCFS\n");
    printf("%s\n", LINE);
    printf("1. Input manual\n");
    for (int i = 0; i < NUM_SCENARIOS; i++) {
        printf("%d. %s\n", i + 2, SCENARIOS[i].name);
    }
    printf("%d. %s\n", NUM_SCENARIOS + 2, EXAMPLE_FROM_DOC.name);
    printf("0. Keluar\n");

    int choice = read_int_range("Pilih menu: ", 0, NUM_SCENARIOS + 2);

    if (choice == 0) {
        return 0;
    } else if (choice == 1) {
        n = read_int_range("Jumlah proses: ", 1, MAX_PROCESS);
        read_config(cfg);
        for (int i = 0; i < n; i++) {
            int at, bt;
            read_at_bt(i + 1, &at, &bt);
            init_pcb(&processes[i], i + 1, at, bt);
        }
    } else if (choice <= NUM_SCENARIOS + 1) {
        n = load_scenario(&SCENARIOS[choice - 2], processes, cfg);
    } else {
        n = load_scenario(&EXAMPLE_FROM_DOC, processes, cfg);
    }

    *trace = read_yes_no(
        "Tampilkan isi queue (linked list) tiap scheduler memilih proses? (y/n): ");
    return n;
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

/* ======================================================================
 * BAGIAN 3, 4, 5 - Scheduling Table, Rata-rata, CPU Util & Throughput
 * ====================================================================== */

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

/* ======================================================================
 * LOOP SCHEDULER MLFQ + main()  ->  dikerjakan Anggota 1
 *
 * Fungsi siap pakai dari bagian lain:
 *   input_menu(processes, &cfg, &trace)      -> n (0 = keluar)
 *   print_process_input(processes, n, &cfg)
 *   mlfq_init_queues(queues, &cfg)  reset_mlfq_logs()  reset_boost_logs()
 *   log_init(&log)
 *   mlfq_make_ready(queues, p, level, t)     -> state READY + enqueue + log
 *   mlfq_highest_ready(queues)               -> level tertinggi / -1
 *   queue_peek(&queues[lv])  queue_dequeue(&queues[lv])
 *   boost_due(&cfg, t)  priority_boost(queues, running, t)
 *   record_process_state()  record_queue_migration()
 *   record_higher_queue_preemption()  log_add_slice()
 *   mlfq_print_queues(queues, t)             -> jika trace aktif
 *   mlfq_free_queues(queues)                 -> di akhir simulasi
 *
 * Setelah simulasi: hitung_metrics(), print_gantt_chart(),
 *   print_process_queue_movements(), print_queue_migrations(),
 *   print_priority_boosts() (jika boost aktif),
 *   print_higher_queue_preemptions(), print_scheduling_table(),
 *   print_averages(), print_util_throughput(),
 *   print_context_switch_info(), print_process_state_transitions()
 * ====================================================================== */