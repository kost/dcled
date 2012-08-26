# This file is part of dcled, written on Sun Jan  4 00:18:16 PST 2009
# Jeff Jahr <malakais@pacbell.net> -jsj 

# What goes into the archive?
DISTFILES= dcled.c makefile README README-MACOS 40-dcled.rules

# INSTALLDIR is where the binaries get installed
INSTALLDIR=/usr/local/bin
FONTDIR="/usr/local/share/dcled"
DCLEDVERSION="2.0"
DIST=dcled-$(DCLEDVERSION)

# If gcc isnt your compiler, change it here.
 
CC=gcc

CFLAGS= -g -O3 -DFONTDIR='$(FONTDIR)' -DDCLEDVERSION='$(DCLEDVERSION)'
LDFLAGS= -g -lm -lhid
 
# You probaby dont need to change anything below this line...
 
# List of the various files
CFILES= dcled.c
HFILES= 
OFILES= dcled.o

# build everything
all:	dcled

dcled: dcled.o
	$(CC) dcled.o -o dcled $(LDFLAGS)

# rebuild the ctags
ctags: $(HFILES) $(CFILES)
	ctags -d -I -l c -t $(HFILES) $(CFILES)

# remove the object files
clean:	
	rm -i $(OFILES) dcled

# copy stuff into the install directory
install:
	mkdir -p $(FONTDIR)
	cp fonts/*.dlf $(FONTDIR)
	mkdir -p $(INSTALLDIR)
	cp dcled $(INSTALLDIR)
	#
	# Now run 'make udev' if you want to install the device permissions.
	#

udev:
	cp 40-dcled.rules /lib/udev/rules.d
	service udev restart
	# Done!

dist:
	mkdir ${DIST}
	cp ${DISTFILES} ${DIST}
	cp -r fonts ${DIST}
	tar -cvzf ${DIST}.tgz ${DIST}

# ...and now the dependencies. 
dcled.o : dcled.c
	$(CC) -c $(CFLAGS) dcled.c

# Still reading?  Then the problem probably isnt with this file. ;) -jsj
