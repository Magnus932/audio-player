
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generic.h"

void init_mutex_cond(music_player_t *music)
{
	pthread_mutex_init(&music->mutex, NULL);
	pthread_cond_init(&music->cond, NULL);
}

int main(int argc, char **argv)
{
	music_player_t *music;

	music = (music_player_t *)malloc(sizeof(*music));	
	memset(music, '\0', sizeof(*music));

	setup_gui(music, &argc, argv);
	init_mutex_cond(music);
	init_decoder(music);
	init_audio(music);

	gtk_main();
}