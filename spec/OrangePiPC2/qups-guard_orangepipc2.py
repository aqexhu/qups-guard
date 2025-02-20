#import RPi.GPIO as GPIO
import orangepi.pc2
from OPi import GPIO

GPIO.setmode(orangepi.pc2.BOARD)

from time import sleep
import os, sys, syslog

syslog.openlog(ident="UPS Guard", logoption=syslog.LOG_PID, facility=syslog.LOG_USER)
syslog.syslog("UPS Guard system started.")

GPIO.setwarnings(False)
#GPIO.setmode(GPIO.BCM)

if len(sys.argv)==1: # usage
    print("Usage: qups-guard <DIP switch 1-2 or 1-2-3 state>")
    print("	<DIP switch> 4 or 6 position switch settings as follows:")
    print("              for 4 position switch P1-P2-P3 settings (OFF:0, ON:1) e.g 101 for 1:ON-2:OFF-3:ON")
    print("              for 6 position switch GT1-GT2   settings (OFF:0, ON:1) e.g 10 for GT1:ON-GT2:OFF")
    quit()
elif len(sys.argv)==2: # assume default qUPS-P-BC-1.2 or qUPS-P-SC-1.3
    dip = sys.argv[1]
    if (len(dip)==2):
	# orangepi pc3   
        pin_pfo = {'10': 11, '01': 16, '11': 29} 
        pin_lim = {'10': 13, '01': 18, '11': 31}
        pin_shd = {'10': 15, '01': 22, '11': 37}

pins=[0,0,0]

GPIO.setup(pin_pfo[dip], GPIO.IN, GPIO.PUD_OFF)
GPIO.setup(pin_lim[dip], GPIO.IN, GPIO.PUD_OFF)
GPIO.setup(pin_shd[dip], GPIO.OUT)
GPIO.output(pin_shd[dip], 1)

while True:
    pfo=GPIO.input(pin_pfo[dip])
    if pins[0] != pfo:
        pins[0] = pfo
        if pfo == 1:
            #print ('Power OK')
            syslog.syslog("Power OK")
        else:
            #print ('Power Not ok')
            syslog.syslog("Power NOT OK")
    lim=GPIO.input(pin_lim[dip])
    if pins[1] != lim:
        pins[1] = lim
        if lim == 1:
            #print ('Capacitor in high charge level')
            syslog.syslog("Capacitor in HIGH charge level")
        else:
            #print ('Capacitor in low charge level')
            syslog.syslog("Capacitor in LOW charge level")
            os.system("sudo /usr/bin/systemctl poweroff -i")
    sleep(1)
