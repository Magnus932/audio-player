
#ifndef AUDIO_H
#define AUDIO_H

#include <pulse/pulseaudio.h>

#define PULSE_MAX_BUF_SIZE				8000

#define SAMPLE_FMT(x) ({\
	int fmt;\
	switch(x) {\
		case AV_SAMPLE_FMT_U8:\
		case AV_SAMPLE_FMT_U8P:\
			fmt = PA_SAMPLE_U8;\
		break;\
		case AV_SAMPLE_FMT_S16:\
		case AV_SAMPLE_FMT_S16P:\
			fmt = PA_SAMPLE_S16LE;\
		break;\
		case AV_SAMPLE_FMT_S32:\
		case AV_SAMPLE_FMT_S32P:\
			fmt = PA_SAMPLE_S32LE;\
		break;\
		case AV_SAMPLE_FMT_FLT:\
		case AV_SAMPLE_FMT_FLTP:\
			fmt = PA_SAMPLE_FLOAT32LE;\
	}\
	fmt;\
})
#define AUDIO(x) (x->song_ctx.audio)
#define AUDIO_ML(x) (AUDIO(x).pa_ml)
#define AUDIO_CTX(x) (AUDIO(x).ctx)
#define AUDIO_STREAM(x) (AUDIO(x).stream)
#define AUDIO_VOL(x) (AUDIO(x).vol)
#define AUDIO_END_STREAM(x) (AUDIO(x).end_stream)
#define AUDIO_LOCK(x) (AUDIO(x).lock)
#define AUDIO_COND(x) (AUDIO(x).cond)
#define AUDIO_LOCK2(x) (AUDIO(x).lock2)
#define AUDIO_COND2(x) (AUDIO(x).cond2)

void ctx_drain_complete(pa_context *ctx,
						void *user_data);
int decode_audio_pkt(music_player_t *music, char *buf);
void stream_drain_complete(pa_stream *stream, int success,
						   void *user_data);
void finish_sink_stream(music_player_t *music);
void stream_write_callback(pa_stream *stream, unsigned long len,
						   void *user_data);
void stream_state_callback(pa_stream *stream, void *user_data);
void init_stream(music_player_t *music, char *name);
void context_state_callback(pa_context *ctx,
							void *user_data);
void init_audio(music_player_t *music);
void audio_set_volume(music_player_t *music);
void audio_resume_song(music_player_t *music);
void audio_pause_song(music_player_t *music);
void audio_stop_song(music_player_t *music);
void audio_close_connection(music_player_t *music);

#endif /* AUDIO_H */