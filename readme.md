#
#  MALIB : MADOCA-PPP Library

MALIB is an open source program package especially for MADOCA[^1]-PPP[^2]. MALIB is fork of RTKLIB[^3].\
MALIB support real-time positioning 'rtkrcv' and post-process positioning 'rnx2rtkp' for various receiver including L6E messages.

[^1]:MADOCA  : Multi-GNSS Advanced Orbit and Clock Augmentation
[^2]:PPP     : Precise Point Positioning
[^3]:RTKLIB  : Open source program package for standard and precise positioning with GNSS (Global Navigation Satellite SYstem) URL: https://www.rtklib.com/

## QUICK start

Extract MALIB.zip or clone the git repository to an appropriate directory <install_dir>.
```sh
git clone https://github.com/JAXA-SNU/MALIB.git
```

<details><summary> rtkrcv (real time processing) </summary>

1. Unzip test data\
   Linux :
   ```sh
   tar -zxvf <install_dir>/data/MALIB_OSS_data.tar.gz -C <install_dir>
   ```
   Test sample data has these features
   * Fixed point data
   * Open-sky
   * 2024/08/22 11:00:00 - 2024/08/22 12:00:00 (GPST)

2. Make rtkrcv
   ```sh
   cd <install_dir>/app/consapp/rtkrcv/gcc/
   make clean
   make
   make install
   cd ./../../../../
   ```

3. Execute rtkrcv (replay processing)
   ```sh
   <install_dir>/bin/rtkrcv -o <install_dir>/bin/rtkrcv.conf
   ```
   Then open rtkrcv ver.1.0.0 console:
   ```console
   ** rtkrcv ver.1.0.0  console (h:help) **
   rtkrcv> start
   rtk server start
   rtkrcv> solution 1
   2024/08/22 11:12:04.0 (PPP   ) N:36.06874286 E:140.12834611 H: 111.262
   2024/08/22 11:12:05.0 (PPP   ) N:36.06874284 E:140.12834611 H: 111.264
   2024/08/22 11:12:06.0 (PPP   ) N:36.06874283 E:140.12834611 H: 111.264
   ...
   ...
   2024/08/22 12:00:30.0 (FIX   ) N:36.06874217 E:140.12834622 H: 112.520
   ^C
   rtkrcv> shutdown
   rtk server shutdown ...
   ```

4. Plot data (e.g. RTKPLOT in RTKLIB)\
   output data path 
   ```sh
   <install_dir>/data/out/rtkrcv_test.pos
   ```
   test data reference point 
   ```
   Latitude   : 36.068742145
   Longitude  : 140.128346910 
   Height     : 112.5059
   ```
</details>

<details><summary> rnx2rtkp (post processing) </summary>

1. Unzip test data (You can skip this step if you already installed in rtkrcv quick start)\
   Linux :
   ```sh
   tar -zxvf <install_dir>/data/MALIB_OSS_data.tar.gz
   ```
   Test sample data has these features
   * Fixed point data
   * Open-sky
   * 2024/08/22 11:00:00 - 2024/08/22 12:00:00 (GPST)

2. Make rnx2rtkp
   ```sh
   cd <install_dir>/app/consapp/rnx2rtkp/gcc/
   make clean
   make
   make install
   cd ./../../../../
   ```

3. Execute rnx2rtkp (replay processing)
   ```
   <install_dir>/bin/rnx2rtkp -k <install_dir>/bin/rnx2rtkp.conf <install_dir>/data/MALIB_OSS_data_obsnav_240822-1100.obs <install_dir>/data/MALIB_OSS_data_obsnav_240822-1100.nav <install_dir>/data/2024235L.209.l6 -o <install_dir>/data/out/rnx2rtkp_test.pos
   ```

4. Plot data (e.g. RTKPLOT in RTKLIB) \
   output data path is
   ```
   <install_dir>/data/out/rnx2rtkp_test.pos
   ```
   test data point is
   ```
   Latitude   : 36.068742145
   Longitude  : 140.128346910 
   Height     : 112.5059
   ```
</details>

## Key Features

* Based on RTKLIB 2.4.3 b34 modified version
* Real-time / Post-Process positioning
* Apply various GNSS satellites (GPS/GLONASS/GALILEO/QZSS/BEIDOU)
* PPP and PPP-AR (Ambiguity Resolution)
* Apply various L6E data format (e.g. ubx/sbf/msj)
* Select frequency depends on the GNSS satellites
* Single-station atmospheric delay correction

> [!NOTE]
> PPP-AR only works on the ionosphere configuration STEC estimation (```pos1-ionoopt       =est-stec```)

## How to use

1. Download antex file / MADOCA archive data (only for post-processing)
2. Setup configuration
3. CUI (rtkrcv/rnx2rtkp)

## Download
Download antex file
* [CDDIS GNSS Product](https://cddis.nasa.gov/Data_and_Derived_Products/GNSS/GNSS_product_holdings.html)
* [IGS File Access](https://files.igs.org/pub/station/general/)

Download MADOCA-PPP archive data 
* [MADOCA Product L6E Archive](https://sys.qzss.go.jp/dod/en/archives/agree_madoca.html)
* [MADOCA Product JAXA Archive](https://mgmds01.tksc.jaxa.jp/) 
    (Real-time products have been discontinued since 2026/03/31.)
>[!TIP]
> GPS week can be checked on this HP. [Click here](https://geodesy.noaa.gov/CORS/resources/gpscals.shtml)

## Configuration

MALIB added these signal select and ignore chi-squared error option
```sh
pos2-siggpsIIR-M =L1C/A-L2P     # GPS BLOCK IIR-M (0:L1C/A-L2P,1:L1C/A-L2C           ,3:L1C/A-L2)
pos2-siggpsIIF   =L1C/A-L2P-L5  # GPS BLOCK IIF   (0:L1C/A-L2P,1:L1C/A-L2C,2:L1C/A-L5,3:L1C/A-L2,4:L1C/A-L2P-L5,5:L1C/A-L2C-L5)
pos2-siggpsIIIA  =L1C/A-L2P-L5  # GPS BLOCK IIIA  (0:L1C/A-L2P,1:L1C/A-L2C,2:L1C/A-L5,3:L1C/A-L2,4:L1C/A-L2P-L5,5:L1C/A-L2C-L5)
pos2-siggal      =E1-E5a-E5b-E6 # Galileo (0:E1-E5a,1:E1-E5b,2:E1-E5a-E5b,3:E1-E5a-E5b-E6)
pos2-sigqzs1_2   =L1C-L5-L2C    # QZSS Block I and II (QZS02,03,04) (0:L1C-L5,1:L1C/A-L2C           ,3:L1C-L5-L2C,4:L1C/A-L2C-L5)
pos2-sigqzs2A    =L1C-L5-L2C    # QZSS Block IIA (QZS1R)            (0:L1C-L5,1:L1C/A-L2C,2:L1C/B-L5,3:L1C-L5-L2C,4:L1C/A-L2C-L5,5:L1C/B-L2C-L5)
pos2-sigqzs3     =L1C-L5        # QZSS Block III (QZS05,06,07)      (0:L1C-L5            ,2:L1C/B-L5)
pos2-sigbds2     =B1I-B3I-B2I   # BeiDou-2 (0:B1I-B3I,1:B1I-B2I,2:B1I-B3I-B2I)
pos2-sigbds3     =B1I-B3I-B2a   # BeiDou-3 (0:B1I-B3I,1:B1I-B2a,2:B1I-B3I-B2a,9:B1C-B2a)

pos2-ign_chierr    =on            # (0:off,1:on)
```

### Configuration Examples
Depending on your use case, use one of the following configurations.

<details><summary> use stream </summary> 

```sh
# str1 : Your GNSS Receiver data
# str2 : Correction data (e.g. RTK/DGNSS/Local Augmentation)
# str3 : Correction data (e.g. Main Correction data)
# str4 : Correction data (e.g. Sub Correction data)

# Input stream type
# 0:off,1:serial,2:file,3:tcpsvr,4:tcpcli,6:ntripcli,7:ftp,8:http,10:udpsvr,11:udpcli
inpstr1-type       =ntripcli    
inpstr2-type       =ntripcli   # if needed
inpstr3-type       =ntripcli
inpstr4-type       =tcpcli     # if needed

# Input stream path
inpstr1-path       =[user[:passwd]]@addr:port/mntpnt
inpstr2-path       =[user[:passwd]]@addr:port/mntpnt   # if needed                 
inpstr3-path       =[user[:passwd]]@addr:port/mntpnt
inpstr4-path       =tcpcli://localhost:11000           # if needed  

# Input stream format 
# 0:rtcm2,1:rtcm3,2:oem4,3:oem3,4:ubx,5:ss2,6:hemis,7:skytraq,8:javad,9:nvs
# 10:binex,11:rt17,12:sbf,14:sp3,15:l6,20:stat
inpstr1-format     =rtcm3       
inpstr2-format     =stat   # if needed 
inpstr3-format     =rtcm3    
inpstr4-format     =l6     # if needed 

# SSR data type (0:main,1:sub,-1:off)
# (Each of MAIN and SUB can be assigned only once across all streams)
inpstr1-ssr        =off
inpstr2-ssr        =off
inpstr3-ssr        =main
inpstr4-ssr        =sub
```
</details>

<details><summary> use replay on rtkrcv </summary> 

Add the following option ```::T::x(time)::+(offset)```
```sh
# e.g. skip replay data for 10s and offset +30 sec
inpstr1-type       =file
inpstr1-path       =datapath/dataname.rtcm3::T::x10::+30
```
> [!WARNING]
> when you use MADOCA Product as ephemeris\
> ```pos1-sateph        =brdc+ssrapc```\
> then, ``` pos2-ign_chierr = on ```
</details>

<details><summary> use MADOCA-PPP Ionospheric Correction </summary> 

```sh
pos2-ionocorr      =on
```
> [!WARNING]
> If you use L6D, make sure to use L6E.
</details>

<details><summary> use INS data </summary> 

```sh
pos1-dynamics      =2  # Receiver Dynamics (0:off,1:dynamics,2:ins)
```
</details>

## CUI (rtkrcv/rnx2rtkp)

### rtkrcv : Real-time Positioning

Useage
```
rtkrcv [-s][-p port][-d dev][-o file][-w pwd][-r level][-t level][-sta sta]
```

<details><summary> Options and example command </summary>

Options
```sh
  -s          start RTK server on program startup
  -p port     port number for telnet console
  -m port     port number for monitor stream
  -d dev      terminal device for console
  -o file     processing options file
  -w pwd      login password for remote console ("": no password)
  -r level    output solution status file (0:off,1:states,2:residuals)
  -t level    debug trace level (0:off,1-5:on)
  -opkf level output PPP kalman filter state/covariance
              (0:off,1:states,2:states+dcov,3:states+dcov+ccov) [0]
  -sta sta    station name for receiver dcb
  -v|-ver     print version
  -rst ds ts  start day/time (ds=y/m/d ts=h:m:s) [raw/rtcm data start time]
  -ignsync    ignore time sync for input files [sync]
```

Example

```sh
cd ./app/consapp/rtkrcv/gcc/
make clean
make
./rtkrcv
```

console
```sh
** rtkrcv ver.2.4.3 b35 console (h:help) **
rtkrcv> help 
rtkrcv (ver.2.4.3 b35)
start                 : start rtk server
stop                  : stop rtk server
restart               : restart rtk sever
solution [cycle]      : show solution
status [cycle]        : show rtk status
satellite [-n] [cycle]: show satellite status
observ [-n] [cycle]   : show observation data
navidata [cycle]      : show navigation data
stream [cycle]        : show stream status
ssr [cycle]           : show ssr corrections
error                 : show error/warning messages
option [opt]          : show option(s)
set opt [val]         : set option
load [file]           : load options from file
save [file]           : save options to file
log [file|off]        : start/stop log to file
help|? [path]         : print help
exit|ctr-D            : logout console (only for telnet)
shutdown              : shutdown rtk server
!command [arg...]     : execute command in shell 
```
</details>

### rnx2rtkp : Post-Processing Analysis
Usage
```sh
rnx2rtkp [option]... file file [...]
```
<details><summary> Options and example command </summary>

Options
```sh
 -?              print help
 -k file         input options from configuration file [off]
 -o file         set output file [stdout]
 -ts ds ts       start day/time (ds=y/m/d ts=h:m:s) [obs start time]
 -te de te       end day/time   (de=y/m/d te=h:m:s) [obs end time]
 -ti tint        time interval (sec) [all]
 -p mode         mode (0:single,1:dgps,2:kinematic,3:static,4:moving-base,
                       5:fixed,6:ppp-kinematic,7:ppp-static) [2]
 -m mask         elevation mask angle (deg) [15]
 -sys s[,s...]   nav system(s) (s=G:GPS,R:GLO,E:GAL,J:QZS,C:BDS,I:IRN) [G|R]
 -f freq         number of frequencies for relative mode
                 (1:L1,2:L1+L2,3:L1+L2+L5) [2]
 -v thres        validation threshold for integer ambiguity (0.0:no AR) [3.0]
 -b              backward solutions [off]
 -c              forward/backward combined solutions [off]
 -i              instantaneous integer ambiguity resolution [off]
 -h              fix and hold for integer ambiguity resolution [off]
 -e              output x/y/z-ecef position [latitude/longitude/height]
 -a              output e/n/u-baseline [latitude/longitude/height]
 -n              output NMEA-0183 GGA sentence [off]
 -g              output latitude/longitude in the form of ddd mm ss.ss' 
                 [ddd.ddd]
 -t              output time in the form of yyyy/mm/dd hh:mm:ss.ss [sssss.ss]
 -u              output time in utc [gpst]
 -d col          number of decimals in time [3]
 -s sep          field separator [' ']
 -r x y z        reference (base) receiver ecef pos (m)
                 [average of single pos]
                 rover receiver ecef pos (m) for fixed or ppp-fixed mode
 -l lat lon hgt  reference (base) receiver latitude/longitude/height (deg/m)
                 rover latitude/longitude/height for fixed or ppp-fixed mode
 -ign_chierr     ignore chi-square error mode [off]
 -sta staname    station name[RINEX MARKER NAME]
 -sbfatt attfile septentrio ASCII GNSS attitude(AttEuler) file name
 -sbfimu imufile septentrio ASCII IMU sensor(ExtSensorMeas) file name
 -ubxins ubxfile u-blox F9 high precision sensor fusion GNSS file name
 -subssr ssrfile substitute PPP correction SSR file name
 -y level        output soltion status (0:off,1:states,2:residuals) [0]
 -x level        debug trace level (0:off) [0]
 -opkf level     output PPP kalman filter state/covariance
                 (0:off,1:states,2:states+dcov,3:states+dcov+ccov) [0]
 -ver            print version
```

Example

```sh
cd ./app/consapp/rnx2rtkp/gcc/
make clean
make
./rnxrtkp
```
</details>

## Test Data
### Static GNSS data
- Receiver : Trimble NetR9 Bundle
- Sampling Rate : 1Hz
- GNSS Constellations : GPS, GLONASS, Galileo, QZSS, BeiDou (GREJC) 
- Frequencies : Triple-frequency (BDS:dual-frequency)
- Antenna / radome : SEPCHOKE_B3E6 / SPKE

The rnx2rtkp dataset can be downloaded from the following website:
ftp://mgmds01.tksc.jaxa.jp/data/sample/MALIB_OSS_rnx2rtkp_dataset.tar.gz
### Reference
| lat     | lon     | hight      |
|:-----------|:------------|:------------|
|36.070584997|140.127393622    | 66.9014 |

## Contributor
Tomoji Takasu\
TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION

## Reference
* [RTKLIB](https://www.rtklib.com/) : Open source e program package for standard and precise positioning with GNSS (global
navigation satellite system).[RTKLIB manual](https://www.rtklib.com/prog/manual_2.4.2.pdf) is here.
* [MADOCA-LIB](https://github.com/QZSS-Strategy-Office/madocalib) : Open source software for MADOCA-PPP (only post-processing)

## Directories Structure

<details><summary> tree structure of MALIB </summary>

```shell
.
├── LICENSE.txt
├── app/
│  ├── consapp/
│  │   ├── makefile
│  │   ├── instrans
│  │   │   └── instrans
│  │   ├── lclcmbpp/
│  │   │   ├── lclcmbpp.c
│  │   │   └── gcc/
│  │   │       └── makefile
│  │   ├── lclcmbrt/
│  │   │   ├── lclcmbrt.c
│  │   │   └── gcc/
│  │   │        └── makefile
│  │   ├── recvbias/
│  │   │   ├── recvbias.c
│  │   │   └── gcc/
│  │   │       └── makefile
│  │   ├── rnx2rtkp/
│  │   │   ├── rnx2rtkp.c
│  │   │   ├── gcc/
│  │   │   │   └── makefile
│  │   │   └── gcc_mkl/
│  │   │       └── makefile
│  │   └── rtkrcv/
│  │       ├── rtkrcv.c
│  │       ├── vt.c
│  │       ├── vt.h
│  │       ├── gcc/
│  │       │   └── makefile
│  │       └── gcc_mkl/
│  │           └── makefile
│  └─qtapp/
│      ├── BASELIB_ISSUES.md
│      ├── BUILD.md
│      ├── CMakeLists.txt
│      ├── NOTICE.txt
│      ├── PORTING_NOTES.md
│      ├── appcmn_qt/
│      │   ├── cmdoptdlg.cpp
│      │   ├── cmdoptdlg.h
│      │   ├── ftpoptdlg.cpp
│      │   ├── ftpoptdlg.h
│      │   ├── graph.cpp
│      │   ├── graph.h
│      │   ├── refdlg.cpp
│      │   ├── refdlg.h
│      │   ├── serioptdlg.cpp
│      │   ├── serioptdlg.h
│      │   ├── tcpoptdlg.cpp
│      │   ├── tcpoptdlg.h
│      │   └── textconv.h
│      ├── license/
│      │   ├── LICENSE.GPLv3.txt
│      │   └── LICENSE.LGPLv3.txt
│      ├── rtknavi_qt/
│      │   ├── appstyle.h
│      │   ├── basefix.h
│      │   ├── instrdlg.cpp
│      │   ├── instrdlg.h
│      │   ├── main.cpp
│      │   ├── maincli.cpp
│      │   ├── mondlg.cpp
│      │   ├── mondlg.h
│      │   ├── navimain.cpp
│      │   ├── navimain.h
│      │   ├── optdlg.cpp
│      │   ├── optdlg.h
│      │   ├── outstrdlg.cpp
│      │   ├── outstrdlg.h
│      │   ├── plotpanel.cpp
│      │   ├── plotpanel.h
│      │   ├── snrwdlg.cpp
│      │   └── snrwdlg.h
│      ├── rtkplot_qt/
│      │   ├── conndlg.cpp
│      │   ├── conndlg.h
│      │   ├── main.cpp
│      │   ├── plotdata.cpp
│      │   ├── plotdata.h
│      │   ├── plotmain.cpp
│      │   ├── plotmain.h
│      │   ├── plotopt.cpp
│      │   ├── plotopt.h
│      │   ├── plotwidget.cpp
│      │   └── plotwidget.h
│      ├── spike/
│      │   └── main.cpp
│      └── tools/
│          ├── cmp_pos.py
│          ├── deploy_win.sh
│          ├── gen_testlist.py
│          └── README.md
├── winapp/
│      ├── libgcc_s_seh-1.dll
│      ├── libstdc++-6.dll
│      ├── libwinpthread-1.dll
│      ├── Qt6Core.dll
│      ├── Qt6Gui.dll
│      ├── Qt6Network.dll
│      ├── Qt6Svg.dll
│      ├── Qt6Widgets.dll
│      ├── rtknavi_cli.exe
│      ├── rtknavi_qt.exe
│      ├── rtkplot_qt.exe
│      ├── generic/
│      │   └── qtuiotouchplugin.dll
│      ├── iconengines/
│      │   └── qsvgicon.dll
│      ├── imageformats/
│      │   ├── qgif.dll
│      │   ├── qico.dll
│      │   ├── qjpeg.dll
│      │   └── qsvg.dll
│      ├── networkinformation/
│      │   └── qnetworklistmanager.dll
│      ├── platforms/
│      │   └── qwindows.dll
│      ├── styles/
│      │   └── qmodernwindowsstyle.dll
│      └── tls/
│          ├── qcertonlybackend.dll
│          └── qschannelbackend.dll
├── bin/
│   ├── lclcmb_gridfile_sample.txt
│   ├── lclcmb_stafile_sample.txt
│   ├── lclcmbrt_stset_sample.conf
│   ├── rnx2rtkp_sample.conf
│   └── rtkrcv_sample.conf
├── data/
│   └── igs20_2423.atx
│   └── MALIB_OSS_data.tar.gz
│   └── MALIB_OSS_rtkrcv_dataset.tar.gz
├── doc/
│   └── manual_MALIB1.3.0.pdf
├── lib/
│   └── mkl/
│       ├── readme.txt
│       └── redist.txt
├── readme.md
└── src/
    ├── biassnx.c
    ├── ephemeris.c
    ├── geoid.c
    ├── inscmn.c
    ├── ionex.c
    ├── lambda.c
    ├── lclcmbcmn.c
    ├── lclcmn.c
    ├── mdccssr.c
    ├── mdciono.c
    ├── options.c
    ├── pntpos.c
    ├── postpos.c
    ├── ppp.c
    ├── ppp_ar.c
    ├── ppp_corr.c
    ├── preceph.c
    ├── qzssl6.c
    ├── rcv/
    │   ├── binex.c
    │   ├── crescent.c
    │   ├── javad.c
    │   ├── novatel.c
    │   ├── nvs.c
    │   ├── rt17.c
    │   ├── septentrio.c
    │   ├── skytraq.c
    │   ├── ss2.c
    │   └── ublox.c
    ├── rcvraw.c
    ├── rinex.c
    ├── rtcm.c
    ├── rtcm2.c
    ├── rtcm3.c
    ├── rtcm3e.c
    ├── rtcm3lcl.c
    ├── rtkcmn.c
    ├── rtklib.h
    ├── rtkpos.c
    ├── rtksvr.c
    ├── satqual.c
    ├── sbas.c
    ├── solution.c
    ├── stream.c
    └── tides.c
```
</details>

## UPDATE HISTORY
| Date       | version     | feature      |
|:-----------|------------:|:------------|
| 2024/09/27 | 1.0.0       | first trail version MALIB |
| 2025/02/28 | 1.1.0       | Add single-station atmospheric delay correction|
|            |             | Delete unused files |
| 2025/10/27 | 1.2.0_pre   | Pre-release version for upcoming v1.2.0|
|            |             | Added support for BeiDou Navigation Satellite System (B1C-B2, B1C-B3I (post-processing only))|
|            |             | Added support for QZSS L1C/B and Galileo E5b signal selection|
|            |             | Added support for the new MADOCA-PPP format (IS-QZSS-MDC-004)|
|            |             | Added support for RINEX ver. 4.00 and ver. 4.01|
|            |             | Added support for the OSB (Observable Specific Bias) product format|
|            |             | Fixed some bags|
| 2026/10/23 | 1.3.0       | Added support for L6E substitute PPP option
|            |             | Added support for L6D wide-area ionospheric correction information
|            |             | Added support for BDS-2 and BDS-3
|            |             | Added support for triple/quad-frequency PPP and PPP-AR
|            |             | Added support for Phase Bias in BIAS-SINEX
|            |             | Added support for septentrio/u-blox INS data
|            |             | Added support for tightly coupled PPP/INS integration
|            |             | Added support for NLOS observation exclusion
|            |             | Change priority of utilized signals
|            |             | Updated handling of PCO/PCV for multi-GNSS and multi-frequency
|            |             | Other fixes to bugs, log levels, and comments
|            |             | Added RTKNAVI and RTKPLOT to the Windows app


## FAQ
TBD...

## Annotation
To ensure optimal performance, we recommend the following system configuration

#### Operating System : Ubuntu 20.04
The software is primarily developed and tested on native Ubuntu 20.04 environments. \
However, it can also run on Windows environments via WSL (Windows Subsystem for Linux)
using Ubuntu.

## Waring
Using L6E versions released on or after July 2, 2026 with the `feature/1.2.0` branch will cause the application to terminate unexpectedly. \
This issue occurs because BeiDou support has not been implemented. \
Please use `main(ver.1.3.0)` branch instead.
