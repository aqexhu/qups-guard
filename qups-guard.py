import RPi.GPIO as GPIO
from time import sleep
import os, sys, syslog

syslog.openlog(ident="UPS Guard", logoption=syslog.LOG_PID, facility=syslog.LOG_USER)
syslog.syslog("UPS Guard system started.")

GPIO.setwarnings(False)
GPIO.setmode(GPIO.BOARD)

if len(sys.argv)==2:
    dip = sys.argv[1]
else:
    dip = input('DIP position (ON:1, OFF:0)')

pin_pfo = {'111': 7, '011': 8, '101': 22, '001': 11 , '110': 19, '010': 32, '100': 35}
pin_lim = {'111': 18, '011': 12, '101': 26, '001': 15 , '110': 23, '010': 38, '100': 40}
pin_shd = {'111': 16, '011': 10, '101': 24, '001': 13 , '110': 21, '010': 36, '100': 37}


pins=[0,0,0]

GPIO.setup(pin_pfo[dip], GPIO.IN)
GPIO.setup(pin_lim[dip], GPIO.IN)
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