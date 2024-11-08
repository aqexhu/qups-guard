import RPi.GPIO as GPIO
from time import sleep
import os, sys, syslog

syslog.openlog(ident="UPS Guard", logoption=syslog.LOG_PID, facility=syslog.LOG_USER)
syslog.syslog("UPS Guard system started.")

GPIO.setwarnings(False)
GPIO.setmode(GPIO.BCM)

if len(sys.argv)==1: # usage
    print("Usage: qups-guard <DIP switch 1-2 or 1-2-3 state>")
    print("	<DIP switch> 4 or 6 position switch settings as follows:")
    print("              for 4 position switch P1-P2-P3 settings (OFF:0, ON:1) e.g 101 for 1:ON-2:OFF-3:ON")
    print("              for 6 position switch GT1-GT2   settings (OFF:0, ON:1) e.g 10 for GT1:ON-GT2:OFF")
    quit()
elif len(sys.argv)==2: # assume default qUPS-P-BC-1.2 or qUPS-P-SC-1.3
    dip = sys.argv[1]
    pin_pfo = {'10': 17, '01': 23, '11': 5}
    pin_lim = {'10': 27, '01': 24, '11': 6}
    pin_shd = {'10': 22, '01': 25, '11': 26}
elif len(sys.argv)==3: # version and DIP
    if (sys.argv[1]=="1.1"):
        dip = sys.argv[2]
        pin_pfo = {'111': 4, '011': 14, '101': 25, '001': 17 , '110': 10, '010': 12, '100': 19}
        pin_lim = {'111': 24, '011': 18, '101': 7, '001': 22 , '110': 11, '010': 20, '100': 21}
        pin_shd = {'111': 23, '011': 15, '101': 8, '001': 27 , '110': 9, '010': 16, '100': 26}
    if (sys.argv[1]=="1.2") or (sys.argv[1]=="1.3"):
        dip = sys.argv[2]
        pin_pfo = {'10': 17, '01': 23, '11': 5}
        pin_lim = {'10': 27, '01': 24, '11': 6}
        pin_shd = {'10': 22, '01': 25, '11': 26}



pins=[0,0,0]

print(dip)
print(pin_pfo[dip])
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
