#! /bin/sh

#
# dropbox
#
cd c:anchor/cloud_handlers/source/dropbox_handler

make -f makefile.os3 clean
make -f makefile.os3

make -f makefile.os3b clean
make -f makefile.os3b

make -f makefile.os4 clean
make -f makefile.os4
ppc-amigaos-strip devs/dropbox-handler.os4

make -f makefile.mos clean
make -f makefile.mos
ppc-morphos-strip devs/dropbox-handler.mos

make -f makefile.aros clean
make -f makefile.aros
#i386-aros-strip devs/dropbox-handler.aros

#
# google drive
#
cd c:anchor/cloud_handlers/source/google_drive_handler

make -f makefile.os3 clean
make -f makefile.os3

make -f makefile.os3b clean
make -f makefile.os3b

make -f makefile.os3_log clean
make -f makefile.os3_log

make -f makefile.os4 clean
make -f makefile.os4
ppc-amigaos-strip devs/google-drive-handler.os4

make -f makefile.mos clean
make -f makefile.mos
ppc-morphos-strip devs/google-drive-handler.mos

make -f makefile.aros clean
make -f makefile.aros
#i386-aros-strip devs/google-drive-handler.aros
