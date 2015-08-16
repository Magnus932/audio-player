
CFLAGS = `pkg-config --cflags gtk+-3.0` -ggdb
LIBS = -lavcodec -lavformat -lavutil -lpthread -lpulse \
		`pkg-config --libs gtk+-3.0`
objects = main.o decoder.o audio.o

audio:	$(objects)
	gcc -o $@ $(objects) $(LIBS)

main.o:	main.c include/gtk.h include/generic.h
	cc -c $(CFLAGS) main.c

decoder.o:	decoder.c include/decoder.h include/generic.h
	cc -c $(CFLAGS) decoder.c

audio.o:	audio.c include/audio.h include/generic.h
	cc -c $(CFLAGS) audio.c

clean:
	rm *.o