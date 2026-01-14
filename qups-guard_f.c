#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>

pthread_t g_thread, g_shdthread;

struct gpiod_chip *chip;
struct gpiod_line *linePfo;
struct gpiod_line *lineLim;
uint8_t lastval_pfo = 255, lastval_lim = 255;
bool event_pfo, event_lim;
struct gpiod_line *lineShd;
struct timespec ts;
struct timespec start_time, test_time;
struct gpiod_line_request *in_request = NULL;
struct gpiod_line_request *shd_request = NULL;
struct gpiod_edge_event_buffer *evbuf = NULL;

#define CONSUMER "qUPS-guard"
#define POLLINTERVAL 1000
#define SHUTDOWN_DELAY 0
uint8_t shutdown_delay = 0;
static uint8_t shutdown_pulse = 0;

struct DIPsw
{
    const char *DIP;
    unsigned int pfo_n;
    unsigned int lim_n;
    unsigned int shd_n;
} DIP_sw;

char dip_sw[4];

struct DIPsw DIPswa[10] = {
    {"10", 17, 27, 22}, {"01", 23, 24, 25}, {"11", 5, 6, 26}, {"111", 4, 24, 23}, {"011", 14, 18, 15}, {"101", 25, 7, 8}, {"001", 17, 22, 27}, {"110", 10, 11, 9}, {"010", 12, 20, 16}, {"100", 19, 21, 26}};


double diffcltime(struct timespec a, struct timespec b)
{
    long long elapsed_nanoseconds = (b.tv_sec - a.tv_sec) * 1000000000LL +
                                    (b.tv_nsec - a.tv_nsec);

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
                syslog(LOG_INFO, "Limit LOW since %.2f ms - initiating shutdown with delay %d.", dct/1000, shutdown_delay);

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
    /* Use libgpiod v2 line request / edge-event buffer API */
    const int buf_capacity = 64;
    evbuf = gpiod_edge_event_buffer_new(buf_capacity);
    int64_t timeout_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;

    while (true)
    {
        int ret = gpiod_line_request_wait_edge_events(in_request, timeout_ns);
        if (ret == 1)
        {
            int nread = gpiod_line_request_read_edge_events(in_request, evbuf, buf_capacity);
            if (nread <= 0)
                continue;

            size_t nevents = gpiod_edge_event_buffer_get_num_events(evbuf);
            for (size_t i = 0; i < nevents; ++i)
            {
                struct gpiod_edge_event *ev = gpiod_edge_event_buffer_get_event(evbuf, i);
                enum gpiod_edge_event_type et = gpiod_edge_event_get_event_type(ev);
                unsigned int offset = gpiod_edge_event_get_line_offset(ev);

                if (offset == DIP_sw.pfo_n)
                {
                    if (et == GPIOD_EDGE_EVENT_FALLING_EDGE)
                    {
                        if (lastval_pfo != gpiod_line_request_get_value(in_request, DIP_sw.pfo_n))
                            syslog(LOG_INFO, "UPS line power NOK!");
                    }
                    else if (et == GPIOD_EDGE_EVENT_RISING_EDGE)
                    {
                        if (lastval_pfo != gpiod_line_request_get_value(in_request, DIP_sw.pfo_n))
                            syslog(LOG_INFO, "UPS line power OK.");
                    }
                    lastval_pfo = (uint8_t)gpiod_line_request_get_value(in_request, DIP_sw.pfo_n);
                }
                else if (offset == DIP_sw.lim_n)
                {
                    if (et == GPIOD_EDGE_EVENT_FALLING_EDGE)
                    {
                        shutdown_pulse = 1;
                        clock_gettime(CLOCK_MONOTONIC, &start_time);
                        syslog(LOG_INFO, "UPS energy level LOW.");
                    }
                    else if (et == GPIOD_EDGE_EVENT_RISING_EDGE)
                    {
                        syslog(LOG_INFO, "UPS energy level HIGH.");
                        shutdown_pulse = 0;
                    }
                    lastval_lim = (uint8_t)gpiod_line_request_get_value(in_request, DIP_sw.lim_n);
                }
            }
        }
        fflush(stdout);
        usleep(POLLINTERVAL);
    }
}

int g_gpiorelease()
{
    if (in_request)
        gpiod_line_request_release(in_request);
    if (shd_request)
        gpiod_line_request_release(shd_request);
    if (evbuf)
        gpiod_edge_event_buffer_free(evbuf);
    gpiod_chip_close(chip);
    return 0;
}

int g_gpioinit()
{
    const char *chippath = "/dev/gpiochip0";
    chip = gpiod_chip_open(chippath);
    if (!chip)
    {
        chippath = "/dev/gpiochip4";
        chip = gpiod_chip_open(chippath);
        if (!chip)
        {
            syslog(LOG_ERR, "Open chip failed\n");
            exit(0);
        }
    }

    struct gpiod_chip_info *info = gpiod_chip_get_info(chip);
    if (info)
    {
        syslog(LOG_INFO, "Chip name: %s - label: %s - %zu lines\n",
               gpiod_chip_info_get_name(info), gpiod_chip_info_get_label(info), gpiod_chip_info_get_num_lines(info));
        gpiod_chip_info_free(info);
    }

    /* Prepare request for output (shutdown) line */
    struct gpiod_request_config *reqcfg_out = gpiod_request_config_new();
    gpiod_request_config_set_consumer(reqcfg_out, CONSUMER);
    struct gpiod_line_settings *settings_out = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings_out, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings_out, GPIOD_LINE_VALUE_ACTIVE);
    struct gpiod_line_config *linecfg_out = gpiod_line_config_new();
    unsigned int out_offsets[1] = {DIP_sw.shd_n};
    gpiod_line_config_add_line_settings(linecfg_out, out_offsets, 1, settings_out);
    shd_request = gpiod_chip_request_lines(chip, reqcfg_out, linecfg_out);
    gpiod_line_settings_free(settings_out);
    gpiod_line_config_free(linecfg_out);
    gpiod_request_config_free(reqcfg_out);

    /* Prepare request for input lines (pfo + lim) */
    struct gpiod_request_config *reqcfg_in = gpiod_request_config_new();
    gpiod_request_config_set_consumer(reqcfg_in, CONSUMER);
    struct gpiod_line_settings *settings_in = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings_in, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings_in, GPIOD_LINE_EDGE_BOTH);
    gpiod_line_settings_set_bias(settings_in, GPIOD_LINE_BIAS_DISABLED);
    struct gpiod_line_config *linecfg_in = gpiod_line_config_new();
    unsigned int in_offsets[2] = {DIP_sw.pfo_n, DIP_sw.lim_n};
    gpiod_line_config_add_line_settings(linecfg_in, in_offsets, 2, settings_in);
    in_request = gpiod_chip_request_lines(chip, reqcfg_in, linecfg_in);
    gpiod_line_settings_free(settings_in);
    gpiod_line_config_free(linecfg_in);
    gpiod_request_config_free(reqcfg_in);

    ts.tv_sec = 10;
    ts.tv_nsec = 0;

    /* Read initial values */
    lastval_pfo = (uint8_t)gpiod_line_request_get_value(in_request, DIP_sw.pfo_n);
    if (lastval_pfo == 0)
        syslog(LOG_INFO, "UPS line power NOK!");
    else
        syslog(LOG_INFO, "UPS line power OK!");

    lastval_lim = (uint8_t)gpiod_line_request_get_value(in_request, DIP_sw.lim_n);
    if (lastval_lim == 0)
        syslog(LOG_INFO, "UPS energy level LOW.");
    else
        syslog(LOG_INFO, "UPS energy level HIGH.");

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

    for (unsigned int i = 1; i < (unsigned int)argc; i++)
    {
        if (strcmp(argv[i], "--shutdown-delay") == 0)
        {
            mbt = true;
            if (i + 1 < (unsigned int)argc)
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
            if (i + 1 < (unsigned int)argc)
            {
                unsigned int dip_len;
                dip_len = strlen(argv[i + 1]);
                if (dip_len == 3 || dip_len == 2)
                {
                    strncpy(dip_sw, argv[i + 1], dip_len);
                    dip_sw[dip_len] = '\0';
                    for (unsigned int j = 0; j < 10; j++)
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
