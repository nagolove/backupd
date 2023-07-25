#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *card_dest = "/run/media/nagolove/4A3F-0331/";

static const char *replacer_love() {
    static char buf[128] = {};
    sprintf(buf, "love");
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
    /*
    {
        .pattern = "$KISS",
        .fun = replacer_kiss,
    },
    {
        .pattern = "$LOVE",
        .fun = replacer_love,
    },
    */
};

// Действия выполняются находясь в каталоге флешки
// $SOME_TEXT -> fun()
static const char *actions[] = {
    "$PUSHD",
    "$CD2SDCARD",
    "git clone /home/nagolove/ $DATE XXX",
    "$POPD"
    /*
    "$DATE Z $DATE YYYY",
    "III$DATE$DATE",
    "$KISS_$DATE",
    "$DATE_$KISS",
    "$KISS $DATE",
    "$DATE $KISS",
    "$KISS",
    "$LOVEDATE$LOVE",
    "$LOVE$KISS",
    // */
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
    char *pbuf = buf;
    const char *pin = in;
    int replacers_num = sizeof(replacers) / sizeof(replacers[0]);

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
            chunks_num++;

            pin += pattern - pin;

            const char *fun_value = replacers[i].fun();
            if (fun_value) {
                chunks[chunks_num] = strdup(fun_value);
                /*strcpy(chunks[chunks_num], replacers[i].fun());*/
                chunks_num++;
            }

            pin += pattern_len;
            found = true;
        }

        if (!found && pin) {
            printf("pin '%s'\n", pin);
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

    //printf("resolve: buf '%s'\n", buf);
    return buf;
}

static void do_actions() {
    int actions_num = sizeof(actions) / sizeof(actions[0]);
    for (int i = 0; i < actions_num; i++) {
        char resolved_action[2048] = {};
        printf("do_actions: '%s'\n", actions[i]);
        strcat(resolved_action, resolve(actions[i]));
        printf("do_actions: %s\n\n", resolved_action);
        //system(resolved_action);
    }
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

static time_t last_backup_time;

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

static void do_backup() {
    printf("target line found\n");

    int errcode;
    errcode = system("udisksctl mount -b /dev/sdb1");
    printf("mount with code %d\n", errcode);

    char accum_path[512] = {};
    strcat(accum_path, card_dest);
    strcat(accum_path, "accum.txt");
    FILE *accum = fopen(accum_path, "a");
    if (!accum) 
        goto _after_cleanup;

    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    fprintf(
        accum, "%.4dy_%.2dm_%dd_%dh-%dm-%ds\n",
        tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec
    );
    fprintf(accum, "unixtime %lu\n", last_backup_time);



    fclose(accum);

_after_cleanup:


    errcode = system("udisksctl unmount -b /dev/sdb1");
    printf("umount with code %d\n", errcode);
}

/*
    Попытаться прочитать с флешки последнее время обновления

    Если не успешно, то впасть в цикл чтения событий udev

    Если разница с текущим временем больше установленной(30 минут к примеру),
    то произвести резервное копирование.

    Записать на флешку время последнего копирования.

*/

int main() {

    /*
    time_t last_time = 0;
    if (read_last_backup_time(&last_time)) {
        printf("read_last_backup_time: success\n");
    }
    printf("time %s\n", ctime(&last_time));
    */

    do_actions();

    printf("exit(1);\n");
    exit(1);

    FILE *pipe = popen("udevadm monitor", "r");

    if (!pipe) {
        printf("could not open pipe %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    char line[1024 * 2] = {};
    fgets(line, sizeof(line) - 1, pipe);

    do {
        printf("line %s\n", line);
        // ^UDEV  [5390.581842] change   /devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0/host5/target5:0:0/5:0:0:0/block/sdb (block)
        if (strstr(line, "UDEV") && 
            strstr(line, "/devices/pci0000:00/") &&
            strstr(line, "block/sdb")) {
            do_backup();
        }
        fgets(line, sizeof(line) - 1, pipe);
    } while (strlen(line));

    pclose(pipe);
}
