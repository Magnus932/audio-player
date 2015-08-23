
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generic.h"

void init_decoder(music_player_t *music)
{
	av_register_all();
	av_log_set_level(AV_LOG_QUIET);
	pkt_queue_init(music);
}

AVCodecContext *get_audio_codec(music_player_t *music, char *name,
							    int id)
{
	AVCodecContext *ctx;
	AVCodec *codec;

	codec = avcodec_find_decoder(DECODER_CTX(music)->streams[id]->codec->
											 codec_id);
	if (!codec) {
		fprintf(stderr, "Error; Decoder for song %s not found\n");
		return NULL;
	}
	ctx = avcodec_alloc_context3(codec);
	avcodec_copy_context(ctx, DECODER_CTX(music)->streams[id]->codec);
	avcodec_open2(ctx, codec, NULL);

	return ctx;
}

int init_song_context(music_player_t *music, char *name)
{
	int i;

	DECODER_CTX(music) = avformat_alloc_context();
	if (!DECODER_CTX(music)) {
		fprintf(stderr, "Error; Decoder context failed to allocate.\n");
		return 0;
	}
	if (avformat_open_input(&DECODER_CTX(music), name, NULL, NULL) != 0) {
		fprintf(stderr, "Error; %s is not a audio file\n", name);
		goto err;
	}
	if (avformat_find_stream_info(DECODER_CTX(music), NULL) < 0) {
		fprintf(stderr, "Error; %s does not contain any stream\n",
				name);
		goto err;
	}
	/*
	 * Actually loop through all the present streams
	 * and look for a stream of type AVMEDIA_TYPE_AUDIO
	 * so we can find its stream identifier.
	 */
	DECODER_STREAM_ID(music) = -1;
	for (i = 0; i < DECODER_CTX(music)->nb_streams; i++)
		if (DECODER_CTX(music)->streams[i]->codec->codec_type 
			== AVMEDIA_TYPE_AUDIO)
			/*
			 * We found an audio stream!. Pick up its
			 * identifier.
			 */
			DECODER_STREAM_ID(music) = i;

	if (DECODER_STREAM_ID(music) == -1) {
		fprintf(stderr, "Error; %s does not contain any audio stream\n",
				name);
		goto err;
	}
	/*
	 * Get the AVCodecContext that we will use
	 * to decode this audio stream.
	 */
	DECODER_CODEC_CTX(music) = get_audio_codec(music, name,
										   	   DECODER_STREAM_ID(music));
	if (!DECODER_CODEC_CTX(music))
		goto err;
	return 1;

err:
	avformat_free_context(DECODER_CTX(music));
	DECODER_CTX(music) = NULL;
	return 0;
}

void cleanup_decoder(music_player_t *music)
{
	avformat_close_input(&DECODER_CTX(music));
	avformat_free_context(DECODER_CTX(music));
	avcodec_free_context(&DECODER_CODEC_CTX(music));
	DECODER_CTX(music) = NULL;
}

void free_audio_queue_pkts(music_player_t *music)
{
	/*
	 * This is only needed if a song is
	 * being changed. If the stream finishes
	 * automaticly while playing a song, the queue
	 * should be empty.
	 */
	packet_queue_t *queue = DECODER_PKT_QUEUE(music);

	pthread_mutex_lock(&DECODER_QUEUE_LOCK(music));
	AVPacketList *ptr = queue->head;
	while (ptr) {
		queue->head = ptr->next;
		av_free_packet(&ptr->pkt);
		av_free(ptr);
		ptr = queue->head;
	}
	pthread_mutex_unlock(&DECODER_QUEUE_LOCK(music));
}

/*
 * Getting the duration for each song in a large
 * amount of songs makes this routine extremly slow,
 * but it beats having a linked list with resources for
 * each song. Example; 40,000 songs * AVFormatContext +
 * AVCodecContext etc. I couldnt figure out yet if there
 * is any other way using AVCodec to get the stream duration
 * easier without allocating much.
 */
int decoder_get_duration(char *fname)
{
	/*
	 * Routine is responsible for allocating an
	 * AVFormatContext. It is used for figuring
	 * out the duration of a song in a quick way when
	 * a song is added into the playlist. The AVFormatContext
	 * is a dummy value and will be free'd before returning
	 * the duration in a unit of AVStream->time_base->den;
	 * Return value; > 0 on success, 0 on failure.
	 */
	AVFormatContext *ctx;
	AVStream *stream;
	int duration = 0, i, x = -1;

	ctx = avformat_alloc_context();
	if (!ctx)
		goto out;
	if (avformat_open_input(&ctx, fname, NULL, NULL) != 0)
		goto out;
	if (avformat_find_stream_info(ctx, NULL) < 0)
		goto out;

	for (i = 0; i < ctx->nb_streams; i++)
		if (ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
			/*
			 * We found an audio stream!. Pick up its
			 * identifier.
			 */
			x = i;
	if (x == -1)
		goto out;
	duration = get_song_duration(ctx->streams[x]);
out:
	avformat_close_input(&ctx);
	avformat_free_context(ctx);

	return duration;
}

int get_song_duration(AVStream *stream)
{
	return stream->duration / stream->time_base.den;
}

int decode_audio_file(music_player_t *music, char *name)
{
	audio_stop_song(music);

	if (!init_song_context(music, name)) {
		stop_progress_scale(music);
		return 0;
	}
	/*
	 * init_song_context initializes everything
	 * that is needed to retrieve stream packets
	 * and decode an audio stream. Simply because
	 * we are in GTK callback context we will create
	 * a new thread here below for the routine
	 * audio_packet_loop, which in turn will retrieve every
	 * stream packet that will be put on a queue. On the other
	 * end a pulseaudio stream will pull of the queue and decode
	 * them. Finally the audio data are put into a pulseaudio server
	 * buffer, and beamed to the soundcard.
	 */
	if (pthread_create(&DECODER_THREAD_ID(music), NULL,
					   audio_packet_loop, music)) {
		fprintf(stderr, "Error; audio_packet_loop thread failed for song: %s\n",
				name);
		stop_progress_scale(music);
		cleanup_decoder(music);
		return 0;
	}
	init_stream(music, name);

	return 1;
}

int seek_frame(music_player_t *music)
{
	if (music->song_ctx.decoder.seek) {
		av_seek_frame(DECODER_CTX(music), DECODER_STREAM_ID(music),
					  music->song_ctx.decoder.seek_val,
					  music->song_ctx.decoder.seek_flag);
		free_audio_queue_pkts(music);
		music->song_ctx.decoder.seek = 0;
		return 1;
	}
	return 0;
}

/*
 * Todo;
 * - Implement condition blocking instead of restarting
 *   the thread on progress scale usage.
 */
void *audio_packet_loop(void *user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	AVPacket pkt;
	/*
	 * This loop will continue to run until
	 * it returns 0. When it returns 0 all the
	 * packets that contains samples has been
	 * run through.
	 */
	seek_frame(music);
	while (!av_read_frame(DECODER_CTX(music), &pkt)) {
		if (AUDIO_END_STREAM(music))
			break;
		if (seek_frame(music)) {
			av_free_packet(&pkt);
			continue;
		}
		/*
		 * In some cases there might be other streams
		 * beside audio. This is the case if you
		 * load video files into the playlist. If so,
		 * just free the packet.
		 */
		if (pkt.stream_index != DECODER_STREAM_ID(music))
			av_free_packet(&pkt);
		else
			pkt_queue_insert(music, &pkt);
	}
	return NULL;
}

void pkt_queue_free(packet_queue_t *queue)
{
	/*
	 * Will always be called after the queue
	 * is empty.
	 */
	av_free(queue);
}

void pkt_queue_init(music_player_t *music)
{
	DECODER_PKT_QUEUE(music) = av_mallocz(sizeof(packet_queue_t));
	pthread_mutex_init(&DECODER_QUEUE_LOCK(music), NULL);
	pthread_cond_init(&DECODER_QUEUE_COND(music), NULL);
}

int pkt_queue_insert(music_player_t *music, AVPacket *packet)
{
	AVPacketList *entry;
	packet_queue_t *queue = DECODER_PKT_QUEUE(music);

	if ( (entry = av_mallocz(sizeof(AVPacketList))) == NULL) {
		av_free_packet(packet);
		return 0;
	}
	memcpy(&entry->pkt, packet, sizeof(AVPacket));

	pthread_mutex_lock(&DECODER_QUEUE_LOCK(music));
	if (!queue->head)
		queue->head = entry;
	else
		queue->tail->next = entry;
	queue->tail = entry;
	queue->num_pkts++;

	pthread_cond_signal(&DECODER_QUEUE_COND(music));
	pthread_mutex_unlock(&DECODER_QUEUE_LOCK(music));

	return 1;
}

int pkt_queue_get(music_player_t *music, AVPacket *packet)
{
	AVPacketList *ptr;
	packet_queue_t *queue = DECODER_PKT_QUEUE(music);
	struct timespec ts = {
		.tv_sec = time(NULL) + 1
	};
	int retval;

	pthread_mutex_lock(&DECODER_QUEUE_LOCK(music));
	while (1) {
		if (AUDIO_END_STREAM(music))
			goto ret;
		ptr = queue->head;
		if (ptr) {
			memcpy(packet, &ptr->pkt, sizeof(AVPacket));
			queue->head = queue->head->next;
			queue->num_pkts--;
			av_free(ptr);
			break;
		}
		else {
			retval = pthread_cond_timedwait(&DECODER_QUEUE_COND(music),
								   	  	    &DECODER_QUEUE_LOCK(music), &ts);
			if (retval) {
ret:
				pthread_mutex_unlock(&DECODER_QUEUE_LOCK(music));
				return 0;
			}
		}
	}
	pthread_mutex_unlock(&DECODER_QUEUE_LOCK(music));

	return 1;
}

void decoder_update_frame(music_player_t *music,
						  long value, int flag)
{
	AVStream *stream;

	stream = DECODER_CTX(music)->streams[DECODER_STREAM_ID(music)];
	music->song_ctx.decoder.seek = 1;
	music->song_ctx.decoder.seek_val = value * stream->time_base.den;
	music->song_ctx.decoder.seek_flag = flag;

	if (pthread_kill(DECODER_THREAD_ID(music), 0))
		pthread_create(&DECODER_THREAD_ID(music), NULL,
					   audio_packet_loop, music);
}