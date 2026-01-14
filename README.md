# AQEX qUPS smart uninterruptible power supply family
## Software guard for safe shutdown of the Raspberry PI
![qups_trans_v1](https://github.com/aqexhu/qups-guard/blob/main/images/qUPS_products.webp)


![license](https://img.shields.io/github/license/aqexhu/qups-guard)
![last_commit](https://img.shields.io/github/last-commit/aqexhu/qups-guard)
![opsys](https://badgen.net/badge/linux/OK/green?icon=github)

## 1. Greeting / *Üdvözlet*

First of all, we would like to express our deeply honest greeting to all of you, who lay trust in our efforts and use our products! We are working hard day-by-day to keep software and hardware up-to-date and develop the products and ourselves. We are trying to deliver devices and services, which serve your lives and purposes better, than any past ones, taking our environment also in focus. Sustainable, long lifespan, reliable, flexible and easy to use - in a green shape!

*Mindenekelőtt szeretnénk kifejezni őszinte hálánkat mindazoknak, akik bíznak erőfeszítéseinkben és használják termékeinket! Keményen dolgozunk napról napra, hogy a szoftver és a hardver naprakész legyen és velünk együtt fejlődjön. Igyekszünk olyan eszközöket és szolgáltatásokat nyújtani, amelyek minden korábbinál jobban szolgálják az Önök életét és céljait, figyelembe véve a környezetünket is. Fenntartható, hosszú élettartamú, megbízható, rugalmas és könnyen használható - zöld szellemben!*

## 2. Device / *Termék*

The qUPS uninterruptible power supply was created, because the mission critical endpoints need protection against black- or brownouts and during longer power-cuts, syncing/last communication/safe shutdown must be initiated before everything gets dark.
Our solution is simple in design with high flexibility - voltage levels and GPIOs are configurable.

*A qUPS szünetmentes tápegységet azért hoztuk létre, mert a kritikus fontosságú végpontoknak védelemre van szükségük az áramkimaradások vagy áramszünetek ellen, és hosszabb áramkimaradások esetén a szinkronizálást/utolsó kommunikációt/biztonságos leállást el kell indítani, mielőtt minden elsötétül.
Megoldásunk egyszerű felépítésű, nagy rugalmassággal - a feszültségszintek és a GPIO-k konfigurálhatók.*

## 3. Installation / *Telepítés*

### 3.1 RaspiOS on Raspberry PI binary

If you are using RaspiOS, you can clone the repository and start the compiled binaries or execute python3 interpreter. The binaries and the source codes for the c++ version cover two different approaches: qups-guard is a 1 sec polling evaluation for the GPIO, qups-guard_f is an asynchronous event driven version.

*Ha RaspiOS-t használ, akkor klónozhatja a repo-t, és elindíthatja a lefordított binárisokat vagy a python3 értelmezőt. A binárisok és a c++ verzió forráskódjai két különböző megközelítést fednek le: a qups-guard egy 1 másodperces polling kiértékelés a GPIO-ra, a qups-guard_f egy aszinkron eseményvezérelt verzió.*

### 3.2 RaspiOS on Raspberry PI compile from source

Libraries needed:
```
libgpiod-dev
```
Test installation with:
```
apt list --installed | grep libgpio
```
On Raspberry OS based on Debian 13 / Trixie, following is seen:
```
WARNING: apt does not have a stable CLI interface. Use with caution in scripts.

libgpiod-dev/stable,now 2.2.1-2+rpi1+deb13u1 armhf [installed]
libgpiod3/stable,now 2.2.1-2+rpi1+deb13u1 armhf [installed,automatic]
libgpiolib0/stable,now 20251120-1 armhf [installed,automatic]
python3-libgpiod/stable,now 2.2.1-2+rpi1+deb13u1 armhf [installed]
```

Compilation from source provided:

GPIO API v1 (e.g. before debian trixie)
```
/usr/bin/gcc -Wall qups-guard.cpp -o qups-guard -lpthread -lgpiod
```
or
```
/usr/bin/gcc -Wall qups-guard_f.cpp -o qups-guard_f -lpthread -lgpiod
```

GPIO API v2 (debian trixie and later) you need to compile:
```
/usr/bin/gcc -Wall qups-guard2.c -o qups-guard2 -lpthread -lgpiod
```



## 4. Usage / *Használat*

The manually or automatically started qups-guard will keep track of energy level and power supply. 
Manual start from command line (assumed repo cloned to pi home dir and DIP is set to 100):

*A manuálisan vagy automatikusan indított qups-guard nyomon követi az energiaszintet és az áramellátást. 
Kézi indítás parancssorból (feltételezve, hogy a repo a pi home könyvtárba került klónozásra és a DIP 100-ra van állítva):*

```
/home/pi/qups-guard/qups-guard2 --dip 01
```

If power fails, syslog will be updated with the ```Power NOK!``` information. If power keeps failing and the supercapacitor/battery voltage level drops below high voltage limit, the LED changes from **green** to **amber**. After depleting below low voltage limit, **red** LED also starts up and syslog is updated with ```UPS level LOW - initiating shutdown seqence.```

*Ha a tápellátás kiesik, a syslog frissül a ```Power NOK!``` információval. Ha a tápellátás továbbra sem áll helyre, és az energiatároló feszültségszintje a HIGH határérték alá csökken, a LED **zöldről** **borostyánszínűre** változik. Az alacsony feszültséghatár alá történő lemerülés után a **piros** LED is bekapcsol, és a syslog frissül a ```UPS level LOW - initiating shutdown seqence.```*

On power resume, the capacitor gets filled above high voltage limit and the computer gets powered up by the qups.
There is a secondary argument, which can fine-tune the power source to the safe shutdown interval, maximizing availability - it is called ```shutdown-delay``` and can be provided in seconds unit of measure. The histeresis on switching the battery limit from high to low can be different for each battery, therefor this optional parameter can further optimize the usage. Example usage for delaying the shutdown with 10 seconds after battery limit reaches low:

*A tápellátás újraindításakor a kondenzátor a HIGH határérték fölé töltődik, és a számítógépet a qups bekapcsolja.
Van egy másodlagos paraméter, amely finomhangolhatja az áramforrást a biztonságos leállítási intervallumra, maximalizálva a rendelkezésre állást - ezt ```shutdown-delay``-nek nevezik, és másodperc mértékegységben adható meg. Az akkumulátor-határérték HIGH->LOW történő átkapcsolásakor fellépő hiszterézis minden egyes akkumulátor esetében eltérő lehet, ezért ez az opcionális paraméter tovább javíthatja az energiakihasználtságot. Példa a kikapcsolás 10 másodperces késleltetésére, miután az akkumulátor határértéke eléri az alacsony értéket:*

```
/home/pi/qups-guard/qups-guard2 --dip 10 --shutdown-delay 10
```


## 5. Autostart / *Autostart*

You can utilize systemd service model. Therefor the service file is located in the repository "systemd" folder. Be careful, that it is parametrized for python script, user "pi" and with "100" DIP switch setting. Any other combinations are free to use - please take care for the software file adjustments.

*Használhatja a systemd szolgáltatási modellt. Ezért a szolgáltatásfájl a „systemd” mappában található. Vigyázzon, hogy a python szkripthez, a „pi” felhasználóhoz és a „100” DIP-kapcsoló beállításához legyen paraméterezve. Bármilyen más kombináció szabadon használható - ez esetben a fájlokat módosítani kell.*
