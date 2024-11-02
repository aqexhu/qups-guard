#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

struct gpiod_chip *chip;
struct gpiod_line *linePfo;
struct gpiod_line *lineLim;
struct gpiod_line *lineShd;
#define DEBOUNCE_INT 500 * 1000 // 500 ms
u_int8_t lastval_pfo = 255, lastval_lim = 255;
struct timespec ts;

#define CONSUMER "qUPS-guard"
struct DIPsw
{
    const char *DIP;
    uint pfo_n;
    uint lim_n;
    uint shd_n;
} DIP_sw;

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
{"10", 17, 27, 22}, {"01", 23, 24, 25}, {"11", 5, 6, 26},
{"111", 7, 18, 16}, {"011", 8, 12, 10}, {"101", 22, 26, 24}, {"001", 11, 15, 13}, {"110", 19, 23, 21}, {"010", 32, 38, 36}, {"100", 35, 40, 37}
};


void SM()
{
    while (1)
    {
        u_int8_t pfo_val = gpiod_line_get_value(linePfo);
        if (pfo_val != lastval_pfo)
        {
            lastval_pfo = pfo_val;
            switch (pfo_val)
            {
            case 0:
                syslog(LOG_INFO, "Power NOK!");
                continue;
            case 1:
                syslog(LOG_INFO, "Power OK.");
                continue;
            }
        }

        u_int8_t lim_val = gpiod_line_get_value(lineLim);
        if (lim_val != lastval_lim)
        {
            lastval_lim = lim_val;
            switch (lim_val)
            {
            case 0:
                syslog(LOG_INFO, "UPS level LOW - initiating shutdown seqence.");
                if (system("sudo shutdown -h now") == 0)
                {
                    syslog(LOG_INFO, "Shutdown sequence succesfully initiated.");
                }
                continue;
            case 1:
                syslog(LOG_INFO, "UPS level HIGH.");
                continue;
            }
        }
        usleep(500000);
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
    gpiod_line_request_output(lineShd, CONSUMER, 1);

    int retv = gpiod_line_request_input_flags(linePfo, CONSUMER, GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE);
    if (retv == -1)
    {
        syslog(LOG_ERR, "Pfo line event request failed: %s", strerror(errno));
    }

    retv = gpiod_line_request_input_flags(lineLim, CONSUMER, GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE);
    if (retv == -1)
    {
        syslog(LOG_ERR, "Lim line event request failed: %s", strerror(errno));
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
        for (u_int8_t i = 0; i < 10; i++)
        {
            if (!strcmp(argv[1], DIPswa[i].DIP))
            {
		DIP_sw=DIPswa[i];
//                DIP_sw.DIP = DIPswa[i].DIP;
  //              DIP_sw.pfo_n = DIPswa[i].pfo_n;
    //            DIP_sw.lim_n = DIPswa[i].lim_n;
      //          DIP_sw.shd_n = DIPswa[i].shd_n;
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
        SM();
        g_gpiorelease();
    }

    closelog();
    return 0;
}
