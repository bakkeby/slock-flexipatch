# slock - simple screen locker
# See LICENSE file for copyright and license details.

include config.mk

SRC = slock.c ${COMPATSRC}
OBJ = ${SRC:.c=.o}

all: slock

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk arg.h util.h patches.h

config.h:
	cp config.def.h $@

patches.h:
	cp patches.def.h $@

slock: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f slock ${OBJ} slock-${VERSION}.tar.gz

dist: clean
	mkdir -p slock-${VERSION}
	cp -R LICENSE Makefile README slock.1 config.mk \
		${SRC} config.def.h arg.h util.h slock-${VERSION}
	tar -cf slock-${VERSION}.tar slock-${VERSION}
	gzip slock-${VERSION}.tar
	rm -rf slock-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f slock ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/slock
	chmod u+s ${DESTDIR}${PREFIX}/bin/slock
	echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" <slock.1 >${DESTDIR}${MANPREFIX}/man1/slock.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/slock.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/slock
	rm -f ${DESTDIR}${MANPREFIX}/man1/slock.1

.PHONY: all clean dist install uninstall
