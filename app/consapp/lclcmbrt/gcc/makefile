# makefile for lclcmbrt

PROGNAME=lclcmbrt
BINDIR  = ../../../../bin
SRC     = ../../../../src

OPTS  = -DTRACE -DENAGLO -DENAQZS -DENAGAL -DENACMP -DENAIRN -DNFREQ=5 -DNEXOBS=5 -DPROGNAME=\"$(PROGNAME)\"
LIBS  =-lpthread

CFLAGS = -Wall -O3 -ansi -pedantic -Wno-unused-but-set-variable -Wno-stringop-truncation -I$(SRC) $(OPTS) -g
LDLIBS = $(LIBS) -lm -lrt 

all        : $(PROGNAME)
$(PROGNAME): $(PROGNAME).o rtkcmn.o lclcmn.o lclcmbcmn.o stream.o rtcm3.o
$(PROGNAME): rtcm3e.o rtcm.o rtcm2.o rtcm3lcl.o solution.o mdccssr.o mdciono.o geoid.o
$(PROGNAME): rcvraw.o sbas.o qzssl6.o novatel.o ublox.o ss2.o crescent.o skytraq.o
$(PROGNAME): javad.o nvs.o binex.o rt17.o septentrio.o

$(PROGNAME).o : ../$(PROGNAME).c
	$(CC) -c $(CFLAGS) ../$(PROGNAME).c
rtkcmn.o     : $(SRC)/rtkcmn.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/rtkcmn.c
lclcmn.o     : $(SRC)/lclcmn.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/lclcmn.c
lclcmbcmn.o  : $(SRC)/lclcmbcmn.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/lclcmbcmn.c
stream.o     : $(SRC)/stream.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/stream.c
rtcm3.o      : $(SRC)/rtcm3.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/rtcm3.c
rtcm3e.o     : $(SRC)/rtcm3e.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/rtcm3e.c
rtcm.o       : $(SRC)/rtcm.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/rtcm.c
rtcm2.o      : $(SRC)/rtcm2.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/rtcm2.c
rtcm3lcl.o   : $(SRC)/rtcm3lcl.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/rtcm3lcl.c
solution.o   : $(SRC)/solution.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/solution.c
mdccssr.o    : $(SRC)/mdccssr.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/mdccssr.c
mdciono.o    : $(SRC)/mdciono.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/mdciono.c
geoid.o      : $(SRC)/geoid.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/geoid.c
rcvraw.o     : $(SRC)/rcvraw.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/rcvraw.c
sbas.o       : $(SRC)/sbas.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/sbas.c
qzssl6.o     : $(SRC)/qzssl6.c
	$(CC) -c $(CFLAGS) -o $@ $(SRC)/qzssl6.c
novatel.o    : $(SRC)/rcv/novatel.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/novatel.c
ublox.o      : $(SRC)/rcv/ublox.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/ublox.c
ss2.o        : $(SRC)/rcv/ss2.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/ss2.c
crescent.o   : $(SRC)/rcv/crescent.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/crescent.c
skytraq.o    : $(SRC)/rcv/skytraq.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/skytraq.c
javad.o      : $(SRC)/rcv/javad.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/javad.c
nvs.o        : $(SRC)/rcv/nvs.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/nvs.c
binex.o      : $(SRC)/rcv/binex.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/binex.c
rt17.o       : $(SRC)/rcv/rt17.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/rt17.c
septentrio.o : $(SRC)/rcv/septentrio.c
	$(CC) -c $(CFLAGS) $(SRC)/rcv/septentrio.c

$(PROGNAME).o  : $(SRC)/rtklib.h
rtkcmn.o       : $(SRC)/rtklib.h
lclcmn.o       : $(SRC)/rtklib.h
lclcmbcmn.o    : $(SRC)/rtklib.h
stream.o       : $(SRC)/rtklib.h
rtcm3e.o       : $(SRC)/rtklib.h
rtcm3.o        : $(SRC)/rtklib.h
rtcm.o         : $(SRC)/rtklib.h
rtcm2.o        : $(SRC)/rtklib.h
rtcm3lcl.o     : $(SRC)/rtklib.h
solution.o     : $(SRC)/rtklib.h
mdccssr.o      : $(SRC)/rtklib.h
mdciono.o      : $(SRC)/rtklib.h
geoid.o        : $(SRC)/rtklib.h
rcvraw.o       : $(SRC)/rtklib.h
sbas.o         : $(SRC)/rtklib.h
qzssl6.o       : $(SRC)/rtklib.h
novatel.o      : $(SRC)/rtklib.h
ublox.o        : $(SRC)/rtklib.h
ss2.o          : $(SRC)/rtklib.h
crescent.o     : $(SRC)/rtklib.h
skytraq.o      : $(SRC)/rtklib.h
javad.o        : $(SRC)/rtklib.h
nvs.o          : $(SRC)/rtklib.h
binex.o        : $(SRC)/rtklib.h
rt17.o         : $(SRC)/rtklib.h
septentrio.o   : $(SRC)/rtklib.h

clean :
	rm -f $(PROGNAME) $(PROGNAME) *.o *.trace

install :
	cp $(PROGNAME) $(BINDIR)

