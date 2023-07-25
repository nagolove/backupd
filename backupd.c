#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

static const char *card_dest = "/run/media/nagolove/4A3F-0331/";

static const char *replacer_kiss() {
    static char buf[128] = {};
    sprintf(buf, "kiss");
    return buf;
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

static struct {
    const char *pattern;
    const char *(*fun)();
} replacers[] = {
    {
        .pattern = "$DATE",
        .fun = replacer_date,
    },
    {
        .pattern = "$KISS",
        .fun = replacer_kiss,
    },
};

// Действия выполняются находясь в каталоге флешки
static const char *actions[] = {
    "git clone /home/nagolove/ $DATE XXX",
    "$DATE$DATEYYYY",
    "$DATE$DATE",
    "$KISS_$DATE",
    "$KISS",
};

static const char *resolve(const char *in) {
    static char buf[256] = {};
    memset(buf, 0, sizeof(buf));
    char *pbuf = buf;
    const char *pin = in;
    int replacers_num = sizeof(replacers) / sizeof(replacers[0]);
    while (pbuf < buf + sizeof(buf)) {
        for (int i = 0; i < replacers_num; i++) {
            const char *point = strstr(pin, replacers[i].pattern);
            if (point) {
                printf("resolve: point %s\n", point);
                int pattern_len = strlen(replacers[i].pattern);
                printf("resolve: pattern_len %d\n", pattern_len);
                int pattern_pos = point - pin;
                printf("resolve: pattern_pos %d\n", pattern_pos);
                int j = 0;
                printf("resolve: pbuf '%s'\n", pbuf);
                for (j = 0; j < pattern_pos; j++) {
                    *pbuf++ = pin[j];
                }
                printf("resolve: pbuf '%s'\n", pbuf);
                strcat(pbuf, replacers[i].fun());
                printf("resolve: pbuf '%s'\n", pbuf);
                j += pattern_len;
                for (; j < pattern_pos; j++) {
                    *pbuf++ = pin[j];
                }
                printf("resolve: pbuf '%s'\n", pbuf);
                pin += pattern_len;
            }
        }
    }
    printf("resolve: buf '%s'\n", buf);
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
