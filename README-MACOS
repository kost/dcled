Update- 

Robert Flick has written a Homebrew formula for dcled that lives on github and
is maintained by him.  According to Robert:  To install dcled with homebrew
(http://mxcl.github.com/homebrew/) call "brew install dcled" on the command
line.

Being Mac free at the moment, I've not tried it myself, but he says it works
great.  Give it a shot and send us your feedback!

Sat Sep  4 09:35:03 PDT 2010  -jsj 




                  Installing DCLED on a Macintosh (10.5.6)
                                James Bruce

Introduction

   I have create a screen capture of how to install LibUSB, LibHID, and
   install and run DCLED or you can follow the instructions below.

  Requirements

   The system requirements for DCLED running on a Mac are:
     * Mac OS X 10.5.6 or greater (not tested below 10.5.6)
     * C Complier like GNU Complier Collection (GCC)
     * LibUSB
     * LibHID
     * DCLED version 1.5 or above

  Install a C Compiler

    1. Download and install Apple's [1]Xcode which includes GCC or you
       could download and install [2]GCC separately as Xcode is a 1.7GB
       download! Hint: You may find a copy of Xcode on your Mac
       installation disk. Installing GCC separately is beyond the scope of
       this tutorial.

  Installing LibUSB

   Please note you must install LibUSB and LibHID before attempting to
   install DCLED.
    1. Download LibUSB - To save time you can download a pre-compiled
       version of LibUSB from [3]Twain-Sane by Marrias Ellert you will
       need [4]LibUSB 0.1.13 Beta for Leopard 10.5.x
    2. Double-click the .tar.gz file to uncompress.
    3. Double-click the .pkg file and follow the on-screen instructions.

  Installing LibHID

    1. LibHID is available through various channels you can download it
       using SVN or if you haven't got a SVN client you can [5]download a
       .tar version. The following instructions use the .tar option.
    2. Fire up Terminal - you can find this in Macintosh HD > Applications
       > Utilities > Terminal.
    3. Enter the following commands to download and install LibHID.
    4. curl -O
       https://alioth.debian.org/frs/download.php/1958/libhid-0.2.16.tar.g
       z
    5. tar -xvf libhid-0.2.16.tar.gz
    6. cd libhid-0.2.16
    7. ./configure --disable-swig (You may get a python error if you don't
       add the flag --disable-swig)
    8. make
    9. sudo make install
   10. Enter you password and the installation is complete.

  Installing DCLED

    1. Enter the following commands to download and install DCLED.
    2. mkdir dcled (This keeps the uncompressed files organised)
    3. cd dcled/
    4. curl -O http://www.jeffrika.com/~malakai/dcled/dcled-1.9.tgz
    5. tar -xvf dcled01.7.tgz
    6. make
    7. You should now be able to run DCLED, type in ./dcled
    8. If you get an error saying "hid_force_open failed with return code
       7" etc don't worry this means DCLED is working but you have not
       plugged in the USB Display.
    9. Plug in the device and retry: ./dcled
   10. Now type in: Hello World
   11. Hello World will now be scrolled across the display.
   12. I recommend putting the complied dcled file in your /bin directory
       or similar so it can easily be accessed via the command prompt or
       shell scripts.

  Known Issues

     * The Dream Cheeky LED Message Board will not work properly when
       using some USB extension cables, mainly ones with repeaters or
       through some USB hubs.
     * You can only run one Dream Cheeky LED Message Board with DCLED 1.7
       and below. Newer versions may address this small issue.

  Thanks To

     * Jeff Jahr for DCLED, his help and letting us play around with the
       source code.

References

   1. http://developer.apple.com/mac/
   2. http://gcc.gnu.org/
   3. http://www.ellert.se/twain-sane/
   4. http://www.ellert.se/PKGS/libusb-2009-02-22/10.5/libusb.pkg.tar.gz
   5. http://alioth.debian.org/frs/?group_id=30451
