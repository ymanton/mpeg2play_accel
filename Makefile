# Makefile for mpeg2play

# Copyright (C) 1994, MPEG Software Simulation Group. All Rights Reserved.

#
# Disclaimer of Warranty
#
# These software programs are available to the user without any license fee or
# royalty on an "as is" basis.  The MPEG Software Simulation Group disclaims
# any and all warranties, whether express, implied, or statuary, including any
# implied warranties or merchantability or of fitness for a particular
# purpose.  In no event shall the copyright-holder be liable for any
# incidental, punitive, or consequential damages of any kind whatsoever
# arising from the use of these programs.
#
# This disclaimer of warranty extends to the user of these programs and user's
# customers, employees, agents, transferees, successors, and assigns.
#
# The MPEG Software Simulation Group does not represent or warrant that the
# programs furnished hereunder are free of infringement of any third-party
# patents.
#
# Commercial implementations of MPEG-1 and MPEG-2 video, including shareware,
# are subject to royalty fees to patent holders.  Many of these patents are
# general enough such that they are unavoidable regardless of implementation
# design.
#
#

# uncomment this to use OpenGL as the display mechanism
# USE_GL = 1
# USE_NV_FENCE = 1

# use static

# STATIC_LIB = -lXvMCNVIDIA

# use dynamic

 USE_DLOPEN = -DUSE_DLOPEN -DDLFILENAME="\"libXvMCNVIDIA_dynamic.so.1\""


# uncomment the following two lines if you want to use shared memory
# (faster display if server and client run on the same machine)

LIBS = -L. -L/usr/X11R6/lib $(STATIC_LIB) -lXvMC -lXv -lXext -lX11

# uncomment the following line to activate calculation of decoding speed
# (frames per second) and frame rate control (-fn option)

USE_TIME = -DUSE_TIME

# if your X11 include files / libraries are in a non standard location:
# set INCLUDEDIR to -I followed by the appropriate include file path and
# set LIBRARYDIR to -L followed by the appropriate library path and

#INCLUDEDIR = -I/usr/include
#LIBRARYDIR = -L/usr/lib

CC = gcc

CFLAGS = -Wall -O3 -march=i686 -mcpu=i686 -funroll-loops -fomit-frame-pointer $(USE_TIME) $(USE_DLOPEN) $(INCLUDEDIR) 


OBJ = mpeg2dec.o getpic.o motion.o getvlc.o gethdr.o getblk.o getbits.o idct.o

ifeq ($(USE_GL), 1)
ifeq ($(USE_NV_FENCE), 1)
CFLAGS += -DUSE_NV_FENCE
endif
LIBS += -lGL 
OBJ += display_gl.o
else
OBJ += display.o
endif

all: mpeg2play

clean:
	rm -f *.o *% core mpeg2play

mpeg2play: $(OBJ)
	$(CC) $(CFLAGS) $(LIBRARYDIR) -o mpeg2play -rdynamic $(OBJ) $(LIBS)

getbits.o: config.h global.h mpeg2dec.h
getblk.o: config.h global.h mpeg2dec.h
gethdr.o: config.h global.h mpeg2dec.h
getpic.o: config.h global.h mpeg2dec.h
getvlc.o: config.h global.h mpeg2dec.h getvlc.h
idct.o: config.h
motion.o: config.h global.h mpeg2dec.h
mpeg2dec.o: config.h global.h mpeg2dec.h
display.o: config.h global.h mpeg2dec.h
