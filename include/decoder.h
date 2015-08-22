
#ifndef DECODER_H
#define DECODER_H

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#define AVCODEC_MAX_AUDIO_FRAME_SIZE		192000
#define DECODER(x) (x->song_ctx.decoder)
#define DECODER_CTX(x) (DECODER(x).fmt_ctx)
#define DECODER_CODEC_CTX(x) (DECODER(x).codec_ctx)
#define DECODER_STREAM_ID(x) (DECODER(x).id)
#define DECODER_THREAD_ID(x) (DECODER(x).thread_id)
#define DECODER_PKT_QUEUE(x) (DECODER(x).pkt_queue)
#define DECODER_QUEUE_LOCK(x) (DECODER(x).queue_lock)
#define DECODER_QUEUE_COND(x) (DECODER(x).queue_cond)

typedef struct packet_queue {
	AVPacketList *head, *tail;
	int num_pkts;
} packet_queue_t;

void init_decoder(music_player_t *music);
AVCodecContext *get_audio_codec(music_player_t *music,
								char *name, int id);
int init_song_context(music_player_t *music, char *name);
void cleanup_decoder(music_player_t *music);
void free_audio_queue_pkts(music_player_t *music);
int get_song_duration(AVStream *stream);
int decode_audio_file(music_player_t *music, char *name);
int seek_frame(music_player_t *music);
void *audio_packet_loop(void *user_data);
void pkt_queue_free(packet_queue_t *queue);
void pkt_queue_init(music_player_t *music);
int pkt_queue_insert(music_player_t *music, AVPacket *packet);
int pkt_queue_get(music_player_t *music, AVPacket *packet);
void decoder_update_frame(music_player_t *music,
						  long value, int flag);
int decoder_get_duration(gchar *fname);

#endif /* DECODER_H */
