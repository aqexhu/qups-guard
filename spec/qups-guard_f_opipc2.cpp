#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

pthread_t g_thread;

struct gpiod_chip *chip;
struct gpiod_line *linePfo;
#define DEBOUNCE_INT 500 * 1000 // 500 ms
struct gpiod_line *lineLim;
u_int8_t lastval_pfo = 255, lastval_lim = 255;
bool event_pfo, event_lim;
struct gpiod_line *lineShd;
struct timespec ts;

#define CONSUMER "qUPS-guard"
struct DIPsw
{
    const char *DIP;
    uint pfo_n;
    uint lim_n;
    uint shd_n;
} DIP_sw;

// pin_pfo = {'111': 7, '011': 8, '101': 22, '001': 11 , '110': 19, '010': 32, '100': 35}
// pin_lim = {'111': 18, '011': 12, '101': 26, '001': 15 , '110': 23, '010': 38, '100': 40}
// pin_shd = {'111': 16, '011': 10, '101': 24, '001': 13 , '110': 21, '010': 36, '100': 37}

struct DIPsw DIPswa[7] = {
    {"001", 1, 3, 0}, {"010", 200, 198, 201}, {"011", 69, 110, 70}, {"100", 10, 199, 107}, {"101", 2, 21, 13}, {"110", 15, 14, 16}, {"111", 6, 71, 68}};


void *g_callback(void *args)
{
    struct gpiod_line_bulk bulk[2], events[2];
    struct gpiod_line_event ev_g;

    gpiod_line_bulk_add(bulk, linePfo);
    gpiod_line_bulk_add(bulk, lineLim);

    while (true)
    {
        if (gpiod_line_event_wait_bulk(bulk, &ts, events))
        {
            for (u_int8_t i = 0; i < gpiod_line_bulk_num_lines(events); i++)
            {
                struct gpiod_line *line;
                line = gpiod_line_bulk_get_line(events, i);
                if (!line)
                {
                    syslog(LOG_ERR, "Unable to get line %d\n", i);
                    continue;
                }
                gpiod_line_event_read(line, &ev_g);
                if (line == linePfo)
                {
                    // Power
                    if (ev_g.event_type == GPIOD_LINE_EVENT_FALLING_EDGE)
                    {
                        // Power NOK
                        syslog(LOG_INFO, "UPS line power NOK!");
                    }
                    else if (ev_g.event_type == GPIOD_LINE_EVENT_RISING_EDGE)
                    {
                        // Power OK
                        syslog(LOG_INFO, "UPS line power OK.");
                    }
                }
                else if (line == lineLim)
                {
                    // Linit
                    if (ev_g.event_type == GPIOD_LINE_EVENT_FALLING_EDGE)
                    {
                        // Energy limit NOK
                        syslog(LOG_INFO, "UPS energy level LOW - initiating shutdown seqence.");
                        if (system("sudo shutdown -h now") == 0)
                        {
                            syslog(LOG_INFO, "Shutdown sequence succesfully initiated.");
                        }
                    }
                    else if (ev_g.event_type == GPIOD_LINE_EVENT_RISING_EDGE)
                    {
                        // Energy limit OK
                        syslog(LOG_INFO, "UPS energy level HIGH.");
                    }
                }
                usleep(500000);
            }
        }
    }
}

int g_gpiorelease()
{
    gpiod_chip_close(chip);
    return 0;
}

int g_gpioinit()
{
    const char *chipname = "gpiochip1";
    chip = gpiod_chip_open_by_name(chipname);
    if (!chip)
    {
        syslog(LOG_ERR, "Open chip failed\n");
        exit(0);
    }
    syslog(LOG_INFO, "Chip name: %s - label: %s - %d lines\n", gpiod_chip_name(chip), gpiod_chip_label(chip), gpiod_chip_num_lines(chip));
    linePfo = gpiod_chip_get_line(chip, DIP_sw.pfo_n);
    lineLim = gpiod_chip_get_line(chip, DIP_sw.lim_n);
    lineShd = gpiod_chip_get_line(chip, DIP_sw.shd_n);
    gpiod_line_request_output(lineShd, CONSUMER, 1); // TODO

    ts.tv_sec = 10;
    ts.tv_nsec = 0;

    //int retv = gpiod_line_request_both_edges_events_flags(linePfo, CONSUMER, GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE);
    int retv = gpiod_line_request_both_edges_events_flags(linePfo, CONSUMER, GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN);
    if (retv == -1)
    {
        syslog(LOG_ERR, "Pfo line event request failed: %s", strerror(errno));
    }

    //retv = gpiod_line_request_both_edges_events_flags(lineLim, CONSUMER, GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE);
    retv = gpiod_line_request_both_edges_events_flags(lineLim, CONSUMER, GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN);
    if (retv == -1)
    {
        syslog(LOG_ERR, "Lim line event request failed: %s", strerror(errno));
    }

    return 0;
}

int g_gpio_events()
{
    if (pthread_create(&g_thread, NULL, &g_callback, NULL) != 0)
    {
        syslog(LOG_ERR, "Thread init failed.");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    openlog(CONSUMER, LOG_PID | LOG_NDELAY, LOG_USER);
    if (argc == 2)
    {
        syslog(LOG_INFO, "Input argument: %s", argv[1]);
        bool mat = false;
        for (u_int8_t i = 0; i < 7; i++)
        {
            if (!strcmp(argv[1], DIPswa[i].DIP))
            {
                DIP_sw.DIP = DIPswa[i].DIP;
                DIP_sw.pfo_n = DIPswa[i].pfo_n;
                DIP_sw.lim_n = DIPswa[i].lim_n;
                DIP_sw.shd_n = DIPswa[i].shd_n;
                mat = true;
            }
        }
        if (mat)
        {
            syslog(LOG_INFO, "Used pins (BCM) - pfo: %d, lim: %d, shd: %d", DIP_sw.pfo_n, DIP_sw.lim_n, DIP_sw.shd_n);
        }
        else
        {
            syslog(LOG_ERR, "No match found!");
            exit(-1);
        }
    }
    else if (argc < 2)
    {
        syslog(LOG_ERR, "Too few arguments...#1-3 DIP binary pattern needed.");
        exit(0);
    }
    else if (argc > 2)
    {
        syslog(LOG_ERR, "Too many arguments...#1-3 DIP binary pattern needed.");
        exit(0);
    }

    if (!g_gpioinit())
    {
        if (!g_gpio_events())
        {
            pthread_join(g_thread, NULL);
            g_gpiorelease();
        }
    }

    closelog();
    return 0;
}
