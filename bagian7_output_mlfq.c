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
