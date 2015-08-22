
CFLAGS = `pkg-config --cflags gtk+-3.0` -ggdb -Iinclude/
LIBS = -lavcodec -lavformat -lavutil -lpthread -lpulse \
		`pkg-config --libs gtk+-3.0`
objects = main.o gui.o decoder.o audio.o

audio:	$(objects)
	gcc -o $@ $(objects) $(LIBS)

main.o:	src/main.c include/generic.h
	cc -c $(CFLAGS) src/main.c

gui.o:	src/gui.c include/gui.h include/generic.h
	cc -c $(CFLAGS) src/gui.c

decoder.o:	src/decoder.c include/decoder.h include/generic.h
	cc -c $(CFLAGS) src/decoder.c

audio.o:	src/audio.c include/audio.h include/generic.h
	cc -c $(CFLAGS) src/audio.c

install:
	mv audio /usr/bin
clean:
	rm *.o