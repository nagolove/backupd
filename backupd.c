#include <assert.h>
#include <errno.h>
#include <execinfo.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct Script {
    char **lines;
    int lines_num;
};

static const char *script_name = ".backupdrc";
// TODO: Загружать имя каталога карточки из скрипта
static const char *card_dest = "/run/media/nagolove/4A3F-0331/";
// время последней резервной записи
static time_t time_last_backup; 
// промежуток времени, через который делается резервная запись
static time_t time_period;  

static const char *get_script_fname() {
    static char buf[512] = {};
    char *home = getenv("HOME");
    if (home)
        strcat(buf, home);
    strcat(buf, "/");
    strcat(buf, script_name);
    printf("get_script_fname: %s\n", buf);
    return buf;
}

static const char *replacer_kiss() {
    static char buf[128] = {};
    sprintf(buf, "kiss");
    return buf;
}

static char dir_stack[128][128];
static int dir_stack_pos = 0;

static const char *replacer_pushd() {
    char buf[128] = {};
    getcwd(buf, sizeof(buf));
    strcpy(dir_stack[dir_stack_pos++], buf);
    return NULL;
}

static const char *replacer_popd() {
    if (dir_stack_pos) {
        chdir(dir_stack[dir_stack_pos--]);
    }
    return NULL;
}

static const char *replacer_cd2sdcard() {
    chdir(card_dest);
    return NULL;
}

static const char *replacer_date() {
    static char buf[128] = {};
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    sprintf(
        buf, "%.4dy_%.2dm_%dd_%dh-%dm-%ds",
        tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec
    );
    return buf;
}

// XXX: Обрати внимание, как будет работать замена при наличии шаблонов
// $MAKE, $MAK, $MA
// Гарантировано работает только с разными шаблонами.
static struct {
    const char *pattern;
    const char *(*fun)();
} replacers[] = {
    {
        .pattern = "$DATE",
        .fun = replacer_date,
    },
    {
        .pattern = "$CD2SDCARD",
        .fun = replacer_cd2sdcard,
    },
    {
        .pattern = "$PUSHD",
        .fun = replacer_pushd,
    },
    {
        .pattern = "$POPD",
        .fun = replacer_popd,
    },
    {
        .pattern = "$KISS",
        .fun = replacer_kiss,
    },
};

// Находит самый слева расположенный шаблон из всех для данной строки.
// Возвращает указатель на начало найденной строки и значение индекса массива
// replacers с найденным шаблоном.
static const char *find_leftest(const char *in, int *i) {
    assert(in);
    assert(i);
    //printf("find_leftest: '%s'\n", in);
    int replacers_num = sizeof(replacers) / sizeof(replacers[0]);
    char *positions[replacers_num];
    int positions_i[replacers_num];

    memset(positions, 0, sizeof(positions));
    memset(positions_i, 0, sizeof(positions_i));

    for (int j = 0; j < replacers_num; j++) {
        positions[j] = strstr(in, replacers[j].pattern);
        positions_i[j] = j;
    }

    const char *leftest = in + strlen(in);
    //printf("find_leftest: '%s'\n", leftest);
    for (int j = 0; j < replacers_num; j++) {
        if (positions[j] && positions[j] < leftest) {
            leftest = positions[j];
            *i = positions_i[j];
        }
    }
    //printf("find_leftest: leftest '%s'\n", leftest);
    if (strlen(leftest) < 1)
        return NULL;
    return leftest;
}

// CHALLENGE: do resolve() recursive!
static const char *resolve(const char *in) {
    static char buf[256] = {};
    memset(buf, 0, sizeof(buf));
    const char *pin = in;

    int chunks_max = 128;
    char *chunks[chunks_max];
    memset(chunks, 0, sizeof(chunks));
    int chunks_num = 0;

    while (*pin) {
        bool found = false;

        int i = -1;
        const char *pattern = find_leftest(pin, &i);
        //printf("i %d\n", i);
        if (pattern) {
            int pattern_len = strlen(replacers[i].pattern);

            chunks[chunks_num] = strndup(pin, pattern - pin);
            if (chunks_num == chunks_max) 
                goto _too_much_chunks;
            chunks_num++;

            pin += pattern - pin;

            const char *fun_value = replacers[i].fun();
            if (fun_value) {
                chunks[chunks_num] = strdup(fun_value);
                if (chunks_num == chunks_max)
                    goto _too_much_chunks;
                chunks_num++;
            }

            pin += pattern_len;
            found = true;
        }

        if (!found && pin) {
            printf("pin '%s'\n", pin);
            if (chunks_num == chunks_max)
                goto _too_much_chunks;
            chunks[chunks_num++] = strdup(pin);
            break;
        }
    }

    for (int i = 0; i < chunks_num; i++) {
        //printf("chunk[%d] '%s'\n", i, chunks[i]);
        strcat(buf, chunks[i]);
    }

    for (int i = 0; i < chunks_num; i++)
        if (chunks[i])
            free(chunks[i]);

    goto _return;
_too_much_chunks:
    fprintf(stderr, "resolve: too much chunks %d\n", chunks_num);
    exit(EXIT_FAILURE);
_return:
    //printf("resolve: buf '%s'\n", buf);
    return buf;
}

static bool do_actions(char **lines, int lines_num) {
    assert(lines);
    assert(lines_num);
    for (int i = 0; i < lines_num; i++) {
        char resolved_action[2048] = {};
        printf("do_actions: '%s'\n", lines[i]);
        strcat(resolved_action, resolve(lines[i]));
        // Как проверять корректность вызова функций через system()?
        int retcode = system(resolved_action);
        printf("do_actions: system \"%s\" %d\n", resolved_action, retcode);
    }
    return true;
}

static bool has_lock() {
    char lock_path[512] = {};
    strcat(lock_path, card_dest);
    strcat(lock_path, ".backupd_lock");
    FILE *lock = fopen(lock_path, "r");
    if (!lock) 
        return false;
    fclose(lock);
    return true;
}

static bool write_last_backup_time(time_t last_time) {
    bool status = false;

    char accum_path[512] = {};
    strcat(accum_path, card_dest);
    strcat(accum_path, "accum.txt");

    FILE *accum = fopen(accum_path, "a");

    if (!accum) {
        goto _after_cleanup;
    }

    int num = fprintf(accum, "%lu", last_time);
    if (num != 1) {
        fprintf(
            stderr, "write_last_backup_time: could not write time to file\n"
        );
        if (errno)
            fprintf(
                stderr, "write_last_backup_time: error '%s'\n", strerror(errno)
            );
    }

    fclose(accum);
    status = true;

_after_cleanup:
    return status;
}


static bool read_last_backup_time(time_t *last_time) {
    bool status = false;

    char accum_path[512] = {};
    strcat(accum_path, card_dest);
    strcat(accum_path, "accum.txt");

    FILE *accum = fopen(accum_path, "r");

    if (!accum) {
        goto _after_cleanup;
    }

    char line[1024 * 2] = {};
    fgets(line, sizeof(line) - 1, accum);

    char last_line[sizeof(line)];
    while (fgets(line, sizeof(line) - 1, accum)) {
        printf("line: %s\n", line);
        strcpy(last_line, line);
    }

    int handled = sscanf(last_line, "unixtime %lu", last_time);
    if (handled != 1) {
        fprintf(stderr, "could not read proper time\n");
        *last_time = 0;
    }

    fclose(accum);
    status = true;

_after_cleanup:
    return status;
}

// удалить последний символ перевода строки
// XXX: unix-only
static void remove_caret(char *line) {
    int line_len = strlen(line);
    if (line_len > 0 && line[line_len - 1] == '\n') {
        line[line_len - 1] = 0;
    }
}

static void remove_comment(char *line) {
    assert(line);
    while (*line) {
        if (*line == '#') {
            *line = 0;
            break;
        }
        line++;
    }
}

// строка состоит только из пробелов и табуляций?
static bool empty_string(const char *line) {
    while (*line) {
        if (*line != ' ' && *line != '\t')
            return false;
        line++;
    }
    return true;
}

static struct Script script_load(const char *fname) {
    struct Script sc = {};
    FILE *script = fopen(fname, "r");
    if (!script) {
        fprintf(stderr, "script_load: could not load '%s'\n", fname);
        return sc;
    }
    char line[1024] = {};
    int lines_cap = 100;
    sc.lines = calloc(lines_cap, sizeof(sc.lines[0]));
    while (fgets(line, sizeof(line), script)) {
        remove_caret(line);
        remove_comment(line);

        if (empty_string(line))
            continue;

        if (sc.lines_num == lines_cap) {
            lines_cap *= 2;
            sc.lines = realloc(sc.lines, lines_cap * sizeof(sc.lines[0]));
            assert(sc.lines);
        }

        sc.lines[sc.lines_num] = malloc(sizeof(line));
        assert(sc.lines[sc.lines_num]);
        strncpy(sc.lines[sc.lines_num], line, sizeof(line));
        sc.lines_num++;
    }
    fclose(script);
    return sc;
}

static void script_print(struct Script sc) {
    if (!sc.lines)
        return;
    for (int i = 0; i < sc.lines_num; i++) {
        printf("script_print: '%s'\n", sc.lines[i]);
    }
}

static void script_shutdown(struct Script *sc) {
    if (!sc->lines)
        return;
    for (int j = 0; j < sc->lines_num; j++) {
        free(sc->lines[j]);
    }
    free(sc->lines);
    sc->lines = NULL;
    memset(sc, 0, sizeof(*sc));
}

static void mount() {
    // TODO: Путь к устройству задавать в скрипте
    int errcode = system("udisksctl mount -b /dev/sdb1");
    printf("mount: with code %d\n", errcode);
}

static void umount() {
    // TODO: Путь к устройству задавать в скрипте
    int errcode = system("udisksctl unmount -b /dev/sdb1");
    printf("umount: with code %d\n", errcode);
}

static void do_backup() {
    printf("do_backup:\n");

    if (!has_lock()) {
        printf("do_backup: where is no lock\n");
    }

    time_t last_time = 0;
    if (read_last_backup_time(&last_time)) {
         time_last_backup = last_time;
        struct Script sc = script_load(get_script_fname());
        script_print(sc);
        if (do_actions(sc.lines, sc.lines_num)) {
            write_last_backup_time(time(NULL));
        }
        script_shutdown(&sc);
    }
}

static void sig_sigint_handler(int sig) {
    printf("sigint\n");
    exit(EXIT_SUCCESS);
}

static void sig_sigill_handler(int sig) {
    printf("sigill\n");
}

static void sig_sigabrt_handler(int sig) {
    printf("sigabrt\n");
}

static void sig_sigfpe_handler(int sig) {
    printf("sigfpe\n");
}

static void sig_sigsegv_handler(int sig) {
    printf("sigsegv\n");
    int num = 100;
    void *trace[num];
    int size = backtrace(trace, num);
    backtrace_symbols_fd(trace, size, STDERR_FILENO);
    exit(EXIT_FAILURE);
}

static void sig_sigterm_handler(int sig) {
    printf("sigterm\n");
    exit(EXIT_SUCCESS);
}

static void sig_sighup_handler(int sig) {
    printf("sighup\n");
}

static void sig_sigquit_handler(int sig) {
    printf("sigquit\n");
}

static void sig_sigtrap_handler(int sig) {
    printf("sigtrap\n");
}

static void sig_sigkill_handler(int sig) {
    printf("sigkill\n");
}

static void exit_handler() {
    printf("exit_handler:\n");
}

static void loop() {
    const char *pipe_cmd = "udevadm monitor";
    FILE *pipe = popen(pipe_cmd, "r");

    if (!pipe) {
        printf(
            "could not open pipe '%s' with %s\n", pipe_cmd, strerror(errno)
        );
        exit(EXIT_FAILURE);
    }

    char line[1024 * 2] = {};
    fgets(line, sizeof(line) - 1, pipe);

    do {
        printf("line %s\n", line);
        // TODO: Добавить строчки поиска новых устройств в скрипт?
        if (strstr(line, "UDEV") && 
            strstr(line, "add") &&
            strstr(line, "/devices/pci0000:00/") &&
            strstr(line, "block/sdb")) {
            do_backup();
        }
        fgets(line, sizeof(line) - 1, pipe);
    } while (strlen(line));

    pclose(pipe);
}

static void setup_signals() {
    signal(SIGINT, sig_sigint_handler);     /* Interactive attention signal.  */
    signal(SIGILL, sig_sigill_handler);     /* Illegal instruction.  */
    signal(SIGABRT, sig_sigabrt_handler);   /* Abnormal termination.  */
    signal(SIGFPE, sig_sigfpe_handler);     /* Erroneous arithmetic operation.  */
    signal(SIGSEGV, sig_sigsegv_handler);   /* Invalid access to storage.  */
    signal(SIGTERM, sig_sigterm_handler);   /* Termination request.  */
    signal(SIGHUP, sig_sighup_handler);     /* Hangup.  */
    signal(SIGQUIT, sig_sigquit_handler);   /* Quit.  */
    signal(SIGTRAP, sig_sigtrap_handler);   /* Trace/breakpoint trap.  */
    signal(SIGKILL, sig_sigkill_handler);   /* Killed.  */
}

/*
    Попытаться прочитать с флешки последнее время обновления
    Если не успешно, то впасть в цикл чтения событий udev
    Если разница с текущим временем больше установленной(30 минут к примеру),
    то произвести резервное копирование.
    Записать на флешку время последнего копирования.
*/
int main() {
    setup_signals();
    //atexit(exit_handler);
    do_backup();
    loop();
}
