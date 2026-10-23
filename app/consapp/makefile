#
# make all applications
# Rev.2026/02/27 by TETC

export CC = gcc

BINDIR = ../../bin
DIRS   = lclcmbpp lclcmbrt recvbias rnx2rtkp rtkrcv
MKLDIRS= rnx2rtkp rtkrcv

all:
	@for i in $(DIRS); do make -C $$i/gcc; done
#	@for i in $(MKLDIRS); do make -C $$i/gcc_mkl; done

install:
	@echo installing...
	@for i in $(DIRS); do cp $$i/gcc/$$i $(BINDIR); done

clean:
	@echo cleaning up...
	@for i in $(DIRS); do make -C $$i/gcc clean; done
	@for i in $(MKLDIRS); do make -C $$i/gcc_mkl clean; done

