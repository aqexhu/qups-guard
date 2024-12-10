#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>

pthread_t g_thread, g_shdthread;

struct gpiod_chip *chip;
struct gpiod_line *linePfo;
struct gpiod_line *lineLim;
u_int8_t lastval_pfo = 255, lastval_lim = 255;
bool event_pfo, event_lim;
struct gpiod_line *lineShd;
struct timespec ts;
struct timespec start_time, test_time;

#define CONSUMER "qUPS-guard"
#define POLLINTERVAL 1000
#define SHUTDOWN_DELAY 0
u_int8_t shutdown_delay = 0;
static u_int8_t shutdown_pulse = 0;

struct DIPsw
{
    const char *DIP;
    uint pfo_n;
    uint lim_n;
    uint shd_n;
} DIP_sw;

char dip_sw[3];

// qUPS-P-SC-1.1
// pin_pfo = {'111': 7, '011': 8, '101': 22, '001': 11 , '110': 19, '010': 32, '100': 35}
// pin_lim = {'111': 18, '011': 12, '101': 26, '001': 15 , '110': 23, '010': 38, '100': 40}
// pin_shd = {'111': 16, '011': 10, '101': 24, '001': 13 , '110': 21, '010': 36, '100': 37}
//
// qUPS-P-BC-1.2 and qUPS-P-BC-1.3
// pin_pfo = {'10': 17, '01': 23, '11': 5}
// pin_lim = {'10': 27, '01': 24, '11': 6}
// pin_shd = {'10': 22, '01': 25, '11': 26}

struct DIPsw DIPswa[10] = {
    {"10", 17, 27, 22}, {"01", 23, 24, 25}, {"11", 5, 6, 26}, {"111", 4, 24, 23}, {"011", 14, 18, 15}, {"101", 25, 7, 8}, {"001", 17, 22, 27}, {"110", 10, 11, 9}, {"010", 12, 20, 16}, {"100", 19, 21, 26}};

double diffcltime(timespec a, timespec b)
{
    // Calculate the elapsed time in nanoseconds
    long long elapsed_nanoseconds = (b.tv_sec - a.tv_sec) * 1000000000LL +
                                    (b.tv_nsec - a.tv_nsec);

    // Convert nanoseconds to microseconds
    return (double)(elapsed_nanoseconds / 1000.0);
}

void *g_shdcallback(void *args)
{
    while (true)
    {
        if (shutdown_pulse)
        {
            clock_gettime(CLOCK_MONOTONIC, &test_time);
            double dct;
            dct = diffcltime(start_time, test_time);
            if (dct > POLLINTERVAL)
            {
                syslog(LOG_INFO, "Limit LOW since %.2f ms - initiating shutdown with delay %d.", dct/1000, shutdown_delay); //ms
                // Limit was LOW for more than 500msec
                
                sleep(shutdown_delay);
                syslog(LOG_INFO, "Shutdown with delay %d - expired, shutting down.", shutdown_delay);

                if (system("sudo shutdown -h now") == 0)
                {
                    syslog(LOG_INFO, "Shutdown sequence succesfully initiated.");
                }
            }
        }
        fflush(stdout);
        usleep(POLLINTERVAL);
    }
}

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
                        if (lastval_pfo != gpiod_line_get_value(linePfo))
                        {
                            syslog(LOG_INFO, "UPS line power NOK!");
                        }
                    }
                    else if (ev_g.event_type == GPIOD_LINE_EVENT_RISING_EDGE)
                    {
                        // Power OK
                        if (lastval_pfo != gpiod_line_get_value(linePfo))
                        {
                            syslog(LOG_INFO, "UPS line power OK.");
                        }
                    }
                    lastval_pfo = gpiod_line_get_value(linePfo);
                }
                else if (line == lineLim)
                {
                    // Limit
                    if (ev_g.event_type == GPIOD_LINE_EVENT_FALLING_EDGE)
                    {
                        // Energy limit NOK
                        shutdown_pulse = 1;
                        clock_gettime(CLOCK_MONOTONIC, &start_time);
                        syslog(LOG_INFO, "UPS energy level LOW.");
                        continue;
                    }
                    else if (ev_g.event_type == GPIOD_LINE_EVENT_RISING_EDGE)
                    {
                        // Energy limit OK
                        syslog(LOG_INFO, "UPS energy level HIGH.");
                        shutdown_pulse = 0;
                    }
                    lastval_lim = gpiod_line_get_value(lineLim);
                }
                fflush(stdout);
                usleep(POLLINTERVAL);
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
    const char *chipname = "gpiochip0";
    chip = gpiod_chip_open_by_name(chipname);
    if (!chip)
    {
        chipname = "gpiochip4";
        chip = gpiod_chip_open_by_name(chipname);
        if (!chip)
        {
            syslog(LOG_ERR, "Open chip failed\n");
            exit(0);
        }
    }
    syslog(LOG_INFO, "Chip name: %s - label: %s - %d lines\n", gpiod_chip_name(chip), gpiod_chip_label(chip), gpiod_chip_num_lines(chip));
    linePfo = gpiod_chip_get_line(chip, DIP_sw.pfo_n);
    lineLim = gpiod_chip_get_line(chip, DIP_sw.lim_n);
    lineShd = gpiod_chip_get_line(chip, DIP_sw.shd_n);
    gpiod_line_request_output(lineShd, CONSUMER, 1); // TODO

    ts.tv_sec = 10;
    ts.tv_nsec = 0;

    lastval_pfo = gpiod_line_get_value(linePfo);
    int retv = gpiod_line_request_both_edges_events_flags(linePfo, CONSUMER, GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE);
    if (retv == -1)
    {
        syslog(LOG_ERR, "Pfo line event request failed: %s", strerror(errno));
    }
    else
    {
        if (gpiod_line_get_value(linePfo) == 0)
        {
            syslog(LOG_INFO, "UPS line power NOK!");
        }
        else
        {
            syslog(LOG_INFO, "UPS line power OK!");
        }
    }

    retv = gpiod_line_request_both_edges_events_flags(lineLim, CONSUMER, GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE);
    if (retv == -1)
    {
        syslog(LOG_ERR, "Lim line event request failed: %s", strerror(errno));
    }
    else
    {
        if (gpiod_line_get_value(lineLim) == 0)
        {
            syslog(LOG_INFO, "UPS energy level LOW.");
        }
        else
        {
            syslog(LOG_INFO, "UPS energy level HIGH.");
        }
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

    if (pthread_create(&g_shdthread, NULL, &g_shdcallback, NULL))
    {
        syslog(LOG_ERR, "Thread init failed.");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    bool mat = false, mbt = false;
    openlog(CONSUMER, LOG_PID | LOG_NDELAY, LOG_USER);

    for (u_int8_t i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--shutdown-delay") == 0)
        {
            mbt = true;
            if (i + 1 < argc)
            {
                shutdown_delay = atoi(argv[i + 1]);
                i++;
            }
            else
            {
                fprintf(stderr, "Error: --shutdown-delay requires an argument - using default %d.\n", SHUTDOWN_DELAY);
                shutdown_delay = SHUTDOWN_DELAY;
            }
        }
        else if (strcmp(argv[i], "--dip") == 0)
        {
            if (i + 1 < argc)
            {
                u_int8_t dip_len;
                dip_len = strlen(argv[i + 1]);
                if (dip_len == 3 || dip_len == 2)
                {
                    strncpy(dip_sw, argv[i + 1], dip_len);
                    for (u_int8_t j = 0; j < 10; j++)
                    {
                        if (!strcmp(dip_sw, DIPswa[j].DIP))
                        {
                            DIP_sw = DIPswa[j];
                            mat = true;
                        }
                    }
                }
                i++;
            }
            else
            {
                fprintf(stderr, "Error: --dip requires an argument\n");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }

    if (mat)
    {
        syslog(LOG_INFO, "Used pins (BCM) - pfo: %d, lim: %d, shd: %d", DIP_sw.pfo_n, DIP_sw.lim_n, DIP_sw.shd_n);
        printf("Used pins (BCM) - pfo: %d, lim: %d, shd: %d\n", DIP_sw.pfo_n, DIP_sw.lim_n, DIP_sw.shd_n);
    }
    else
    {
        syslog(LOG_ERR, "Used pins not specified - usage: --dip <DIP switch GT1-2 or DIP 1-2-3>");
        syslog(LOG_ERR, "Example: --dip 10  means DIP switch GT1=ON GT2=OFF");
        syslog(LOG_ERR, "         --dip 100 means DIP 1=ON 2=OFF 3=OFF");

        printf("Used pins not specified - usage: --dip <DIP switch GT1-2 or DIP 1-2-3>\n");
        printf("Example: --dip 10  means DIP switch GT1=ON GT2=OFF\n");
        printf("         --dip 100 means DIP 1=ON 2=OFF 3=OFF\n");
    }
    if (!mbt)
    {
        shutdown_delay = SHUTDOWN_DELAY;
    }
    syslog(LOG_INFO, "Shutdown delay %d", shutdown_delay);

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
