
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generic.h"

void ctx_drain_complete(pa_context *ctx,
						void *user_data)
{
	music_player_t *music = (music_player_t *)user_data;

	pa_context_disconnect(ctx);
	pthread_cond_signal(&AUDIO_COND2(music));
}

int decode_audio_pkt(music_player_t *music, char *buf)
{
	static AVPacket pkt;
	static AVFrame frame;
	int len, size, got_audio;
	int bps = 0, audio_buf_offset = 0;

	while (1) {
		while (pkt.size > 0) {
			len = avcodec_decode_audio4(DECODER_CODEC_CTX(music), &frame,
										&got_audio, &pkt);
			if (len < 0)
				/*
				 * If len < 0 we simply break out and fetch
				 * another packet for decoding. The rule of
				 * decode_audio_pkt is that it will never return
				 * without decoded audio data.
				 */
				break;
			pkt.data += len;
			pkt.size -= len;
			if (got_audio) {
				size = av_samples_get_buffer_size(NULL, DECODER_CODEC_CTX(music)->channels,
												  frame.nb_samples,
												  DECODER_CODEC_CTX(music)->sample_fmt,
												  0);
				if (av_sample_fmt_is_planar(DECODER_CODEC_CTX(music)->sample_fmt)) {
					bps = av_get_bytes_per_sample(DECODER_CODEC_CTX(music)->sample_fmt);
					while (audio_buf_offset < size) {
						memcpy((buf + audio_buf_offset), frame.data[0],
							   bps);
						audio_buf_offset += bps;
						memcpy((buf + audio_buf_offset), frame.data[1],
							   bps);
						audio_buf_offset += bps;

						frame.data[0] += bps;
						frame.data[1] += bps;
					}
				}
				else {
					memcpy(buf, frame.data[0], size);
				}
				av_frame_unref(&frame);
				return size;
			}
		}
		if (pkt.data)
			av_free_packet(&pkt);
		/*
		 * Get a packet of the audio queue for
		 * decoding. pkt_queue_get will return 1 if
		 * there was a packet, and 0 on the end of stream.
		 * By convention this routine will also return 0 to
		 * indicate end of stream, and in the stream callback
		 * we will take action to end the stream properly.
		 */
		if (!pkt_queue_get(music, &pkt))
			return 0;
	}
}

void stream_drain_complete(pa_stream *stream, int success,
						   void *user_data)
{
	music_player_t *music = (music_player_t *)user_data;

	pa_stream_disconnect(stream);
	pa_stream_unref(stream);
	AUDIO_STREAM(music) = NULL;
	/*
	 * We also need to cleanup our AVFormatContext
	 * and AVCodecContext. cleanup_decoder will do this
	 * work for us. After return from this routine
	 * everything should be cleaned up for playing a
	 * new song.
	 */
	cleanup_decoder(music);

	/*
	 * Play the next song in the playlist,
	 * if there is any. This is only valid if
	 * AUDIO_END_STREAM is 0. Otherwise the current
	 * stream got interrupted by a new song.
	 */
	if (!AUDIO_END_STREAM(music)) {
		next_song(music);
	}
	else {
		pthread_cond_signal(&AUDIO_COND(music));
	}
}

void finish_sink_stream(music_player_t *music)
{
	pa_stream *stream = AUDIO_STREAM(music);
	pa_operation *op;

	pa_stream_set_write_callback(stream, NULL, NULL);

	op = pa_stream_drain(stream, stream_drain_complete, music);
	pa_operation_unref(op);
}

void stream_write_callback(pa_stream *stream, unsigned long len,
						   void *user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	char *stream_buf, *ptr;
	int len1, stream_len = len;
	static char audio_buf[AVCODEC_MAX_AUDIO_FRAME_SIZE];
	static int audio_buf_offset = 0;
	static int audio_buf_size = 0;

	stream_buf = av_malloc(len);
	ptr = stream_buf;
	while (len > 0) {
		if (audio_buf_offset >= audio_buf_size) {
			audio_buf_size = decode_audio_pkt(music, audio_buf);
			audio_buf_offset = 0;
			if (!audio_buf_size) {
				finish_sink_stream(music);
				return;
			}
		}
		len1 = audio_buf_size - audio_buf_offset;
		if (len1 > len)
			len1 = len;
		memcpy(ptr, (audio_buf + audio_buf_offset), len1);
		audio_buf_offset += len1;
		ptr += len1;
		len -= len1;
	}
	pa_stream_write(stream, stream_buf, stream_len, NULL,
					0, PA_SEEK_RELATIVE);
	av_free(stream_buf);
}

void stream_state_callback(pa_stream *stream, void *user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	pa_stream_state_t state;
	AVStream *av_stream;

	state = pa_stream_get_state(stream);
	switch(state) {
		case PA_STREAM_UNCONNECTED:
		case PA_STREAM_CREATING:
		case PA_STREAM_FAILED:
		case PA_STREAM_TERMINATED:
		break;
		case PA_STREAM_READY:
			av_stream = DECODER_CTX(music)->streams[DECODER_STREAM_ID(music)];
			start_progress_scale(music, get_song_duration(av_stream));
		break;
	}
}

void init_stream(music_player_t *music, char *name)
{
	/*
	 * This will be called from a callback
	 * in GTK context. It initializes a stream
	 * object and sets a write callback.
	 */
	pa_sample_spec sample_spec;
	pa_buffer_attr attr;
	pa_cvolume vol;

	sample_spec.format = SAMPLE_FMT(DECODER_CODEC_CTX(music)->sample_fmt);
	sample_spec.rate = DECODER_CODEC_CTX(music)->sample_rate;
	sample_spec.channels = DECODER_CODEC_CTX(music)->channels;
	AUDIO_STREAM(music) = pa_stream_new(AUDIO_CTX(music), name,
										&sample_spec, NULL);
	pa_stream_set_write_callback(AUDIO_STREAM(music), stream_write_callback,
								 music);
	pa_stream_set_state_callback(AUDIO_STREAM(music), stream_state_callback,
								 music);
	attr.maxlength = PULSE_MAX_BUF_SIZE;
	attr.prebuf = -1;
	attr.tlength = -1;
	attr.minreq = -1;

	pa_cvolume_init(&vol);
	pa_cvolume_set(&vol, DECODER_CODEC_CTX(music)->channels,
				   AUDIO_VOL(music));
	pa_stream_connect_playback(AUDIO_STREAM(music), NULL, &attr,
							   PA_STREAM_NOFLAGS, &vol, NULL);
}

void context_state_callback(pa_context *ctx,
							void *user_data)
{
	pa_context_state_t state;

	state = pa_context_get_state(ctx);
	switch(state) {
		case PA_CONTEXT_UNCONNECTED:
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
		case PA_CONTEXT_FAILED:
		break;
		case PA_CONTEXT_TERMINATED:
		break;
		case PA_CONTEXT_READY:
			fprintf(stderr, "Debug; Connected to pulseaudio server.\n");
		break;
	}
}

void init_audio(music_player_t *music)
{
	/*
	 * This routine creates a connection to
	 * a pulseaudio server ( unix socket ).
	 * If no pulse audio server is running, simply
	 * spawn one.
	 */
	AUDIO_ML(music) = pa_threaded_mainloop_new();
	AUDIO_CTX(music) = pa_context_new(pa_threaded_mainloop_get_api(
									  AUDIO_ML(music)), "Audio");
	pa_context_connect(AUDIO_CTX(music), NULL, 0, NULL);
	pa_context_set_state_callback(AUDIO_CTX(music), context_state_callback,
								  music);
	AUDIO_VOL(music) = PA_VOLUME_NORM;

	/*
	 * Create a lock and a condition variable
	 * that will be used to block execution if
	 * a stream is interrupted.
	 */
	pthread_mutex_init(&AUDIO_LOCK(music), NULL);
	pthread_cond_init(&AUDIO_COND(music), NULL);

	/*
	 * Create a lock and a condition variable
	 * that will be used to block execution if
	 * the context needs to be drained. The mainloop
	 * will be stopped after the draining has completed.
	 */
	pthread_mutex_init(&AUDIO_LOCK2(music), NULL);
	pthread_cond_init(&AUDIO_COND2(music), NULL);

	/*
	 * Lastly we start the threaded mainloop, returning
	 * to GTK context.
	 */
	pa_threaded_mainloop_start(AUDIO_ML(music));
}

void audio_set_volume(music_player_t *music)
{
	pa_cvolume vol;

	if (!AUDIO_STREAM(music))
		return;
	pa_cvolume_init(&vol);
	pa_cvolume_set(&vol, DECODER_CODEC_CTX(music)->channels,
				   AUDIO_VOL(music));
	pa_context_set_sink_input_volume(AUDIO_CTX(music),
									 pa_stream_get_index(AUDIO_STREAM(music)),
									 &vol, NULL, NULL);
}

void audio_resume_song(music_player_t *music)
{
	if (!AUDIO_STREAM(music))
		return;
	if (!pa_stream_is_corked(AUDIO_STREAM(music)))
		return;
	pa_stream_cork(AUDIO_STREAM(music), 0, NULL, NULL);
	resume_progress_scale(music);
}

void audio_pause_song(music_player_t *music)
{
	if (!AUDIO_STREAM(music))
		return;
	if (!pa_stream_is_corked(AUDIO_STREAM(music))) {
		pa_stream_cork(AUDIO_STREAM(music), 1, NULL, NULL);
		pause_progress_scale(music);
	}
}

void audio_stop_song(music_player_t *music)
{
	if (AUDIO_STREAM(music)) {
		/*
	 	 * If song is paused, running this code
	 	 * will create a deadlock because the pulseaudio
	 	 * thread will not pull packets and it will never
	 	 * get to the point where a condition signal will
	 	 * be sent. Unpause the stream if so.
	 	 */
	 	if (pa_stream_is_corked(AUDIO_STREAM(music)))
	 		pa_stream_cork(AUDIO_STREAM(music), 0, NULL, NULL);
		AUDIO_END_STREAM(music) = 1;
		/*
		 * Block waiting for a signal that
		 * indicates that every resource is free'd.
		 */
		pthread_mutex_lock(&AUDIO_LOCK(music));
		pthread_cond_wait(&AUDIO_COND(music), &AUDIO_LOCK(music));
		pthread_mutex_unlock(&AUDIO_LOCK(music));
		free_audio_queue_pkts(music);
		stop_progress_scale(music);
		
		AUDIO_END_STREAM(music) = 0;
	}
}

void audio_close_connection(music_player_t *music)
{
	pa_operation *op;

	op = pa_context_drain(AUDIO_CTX(music), ctx_drain_complete,
						  music);
	if (!op)
		pa_context_disconnect(AUDIO_CTX(music));
	else {
		/*
		 * This is a race condition. We cant stop
		 * the mainloop until the drain callback has
		 * been called.
		 */
		pa_operation_unref(op);
		pthread_mutex_lock(&AUDIO_LOCK2(music));
		pthread_cond_wait(&AUDIO_COND2(music), &AUDIO_LOCK2(music));
		pthread_mutex_unlock(&AUDIO_LOCK2(music));
	}	
	pa_threaded_mainloop_stop(AUDIO_ML(music));
	pa_threaded_mainloop_free(AUDIO_ML(music));
}