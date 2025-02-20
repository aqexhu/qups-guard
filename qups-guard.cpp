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
u_int8_t lastval_pfo = 255, lastval_lim = 255;
u_int8_t shutdown_delay = 0;
char dip_sw[3];
struct timespec ts;

#define CONSUMER "qUPS-guard"
#define POLLINTERVAL 500000
#define DEBOUNCE_COUNT 100
#define DEBOUNCE_INTERVAL 10000
#define SHUTDOWN_DELAY 10

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
{"111", 4, 24, 23}, {"011", 14, 18, 15}, {"101", 25, 7, 8}, {"001", 17, 22, 27}, {"110", 10, 11, 9}, {"010", 12, 20, 16}, {"100", 19, 21, 26}
};

u_int8_t debounce_limit() {
    u_int8_t debcount=0;
    while (++debcount < DEBOUNCE_COUNT) {
        if (gpiod_line_get_value(lineLim)) {
            // lineLim bouncing
            syslog(LOG_INFO, "Limit low returned on %d/%d\n", debcount, DEBOUNCE_COUNT);
            return 1;
        }
        usleep(DEBOUNCE_INTERVAL);
    }
    syslog(LOG_INFO, "Limit low not returned - initiate shutdown.\n");
    u_int8_t shdcnt=shutdown_delay;
    while (shdcnt-->0) {
        syslog(LOG_INFO, "Shutdown delay counter: %d/%d.\n", shdcnt, shutdown_delay);
	sleep(1);
    }
    return 0;
}

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
                break;
            case 1:
                syslog(LOG_INFO, "Power OK.");
                break;
            }
        }

        u_int8_t lim_val = gpiod_line_get_value(lineLim);
        if (lim_val != lastval_lim)
        {
            lastval_lim = lim_val;
            switch (lim_val)
            {
            case 0:
                syslog(LOG_INFO, "UPS level LOW - prepare shutdown seqence.");
		if (debounce_limit()) {
                    syslog(LOG_INFO, "Limit debounce - shutdown postponed.");
		    continue;
		}
                if (system("sudo shutdown -h now") == 0)
                {
                    syslog(LOG_INFO, "Shutdown sequence succesfully initiated.");
                }
                break;
            case 1:
                syslog(LOG_INFO, "UPS level HIGH.");
                break;
            }
        }
        usleep(POLLINTERVAL);
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
    bool mat=false, mbt=false;
    openlog(CONSUMER, LOG_PID | LOG_NDELAY, LOG_USER);

    for (u_int8_t i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shutdown-delay") == 0) {
            mbt=true;
            if (i + 1 < argc) {
                shutdown_delay = atoi(argv[i + 1]);
                i++;
            } else {
                fprintf(stderr, "Error: --shutdown-delay requires an argument - using default %d.\n", SHUTDOWN_DELAY);
                shutdown_delay = SHUTDOWN_DELAY;
            }
        } else if (strcmp(argv[i], "--dip") == 0) {
            if (i + 1 < argc) {
		u_int8_t dip_len;
		dip_len = strlen(argv[i + 1]);
		if (dip_len == 3 || dip_len == 2) {
	                strncpy(dip_sw, argv[i + 1], dip_len);
			for (u_int8_t j=0; j<10; j++) {
				if (!strcmp(dip_sw, DIPswa[j].DIP)) {
					DIP_sw=DIPswa[j];
					mat=true;
				}
			}
		}
                i++;
            } else {
                fprintf(stderr, "Error: --dip requires an argument\n");
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }

    if (mat) {
	syslog(LOG_INFO, "Used pins (BCM) - pfo: %d, lim: %d, shd: %d", DIP_sw.pfo_n, DIP_sw.lim_n, DIP_sw.shd_n);
	printf("Used pins (BCM) - pfo: %d, lim: %d, shd: %d\n", DIP_sw.pfo_n, DIP_sw.lim_n, DIP_sw.shd_n);
    } else {
	syslog(LOG_ERR, "Used pins not specified - usage: --dip <DIP switch GT1-2 or DIP 1-2-3>");
	syslog(LOG_ERR, "Example: --dip 10  means DIP switch GT1=ON GT2=OFF");
	syslog(LOG_ERR, "         --dip 100 means DIP 1=ON 2=OFF 3=OFF");

	printf("Used pins not specified - usage: --dip <DIP switch GT1-2 or DIP 1-2-3>\n");
	printf("Example: --dip 10  means DIP switch GT1=ON GT2=OFF\n");
	printf("         --dip 100 means DIP 1=ON 2=OFF 3=OFF\n");
    }
    if (!mbt) {
	shutdown_delay=SHUTDOWN_DELAY;
    }
    syslog(LOG_INFO, "Shutdown delay %d", shutdown_delay);

    if (!g_gpioinit())
    {
        SM();
        g_gpiorelease();
    }

    closelog();
    return 0;
}
