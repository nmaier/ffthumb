LIBS += $(shell pkg-config --libs libavutil)
LIBS += $(shell pkg-config --libs libavcodec)
LIBS += $(shell pkg-config --libs libavformat)
LIBS += $(shell pkg-config --libs libavfilter)
LIBS += -lpthread -lm

all: ffthumb.dll

ffthumb.dll: thumb.c thumb.h libffthumb.ver
	$(CC) $(CFLAGS) -o $@ -shared $(LDFLAGS) -Wl,--version-script -Wl,libffthumb.ver -Wl,--out-implib -Wl,lib$@.a $< $(LIBS)

install: ffthumb.dll libffthumb.dll.a thumb.h
	cp -f $^ $(PREFIX)/dist/
	$(STRIP) $(PREFIX)/dist/$<

clean:
	rm -f *.dll*

.PHONY: all install clean
