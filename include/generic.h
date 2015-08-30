
#ifndef GENERIC_H
#define GENERIC_H

typedef struct music_player music_player_t;

#include "gui.h"
#include "decoder.h"
#include "audio.h"

typedef struct music_player {
	GtkWidget *window;
	GtkWidget *vbox, *hbox, *hbox1;
	GtkWidget *menu_bar;
	GtkWidget *menu;
	GtkWidget *toolbar;
	GtkWidget *entry, *button;
	GtkWidget *volume_scale;
	GtkWidget *progress_scale;
	GtkWidget *scrolled_window;
	GtkWidget *list_box;
	GtkWidget *list_box_menu;
	GtkWidget *tree_view;
	/*
 	 * These widgets are used as menu items.
 	 */
 	GtkWidget *menu_item_file;
 	GtkWidget *menu_item_open_file;
 	GtkWidget *menu_item_open_folder;
 	GtkWidget *menu_item_separator;
 	GtkWidget *menu_item_close;
 	GtkWidget *menu_item_delete;
	/*
 	 * These widgets are used as toolbar items.
 	 */
 	GtkToolItem *tool_item_play;
 	GtkToolItem *tool_item_pause;
 	GtkToolItem *tool_item_stop;
 	GtkToolItem *tool_item_repeat;
 	GtkToolItem *tool_item_separator;
 	GtkToolItem *tool_item_volume;
	/*
 	 * These datatypes are for use along with
 	 * the tree_view. The references can be retrieved
 	 * using subroutines, but to minimize computization
 	 * references are added here.
 	 */
 	GtkTreeViewColumn *num_song, *song, *dur;
 	GtkCellRenderer *num_rend, *song_rend, *dur_rend;
 	/*
 	 * A counter for the song progress. It counts
 	 * in a unit of seconds.
 	 */
 	guint seconds;
 	/*
 	 * Thread that increments the progress scale in
 	 * a unit of seconds.
 	 */
 	pthread_t tid;
 	pthread_mutex_t mutex;
 	pthread_cond_t cond;
 	guint pause;
 	guint repeat;

 	playlist_t *playlists;
 	playlist_t *playlist;
	/*
	 * This struct is the song context. It
	 * represents the decoder and the playback
	 * structures.
	 */
 	struct {
 		struct {
 			AVFormatContext *fmt_ctx;
 			AVCodecContext *codec_ctx;
 			packet_queue_t *pkt_queue;
 			pthread_mutex_t queue_lock;
 			pthread_cond_t queue_cond;
 			pthread_t thread_id;
 			int id;
 			int seek;
 			long seek_val;
 			int seek_flag;
 		} decoder;
 		struct {
 			pa_threaded_mainloop *pa_ml;
 			pa_context *ctx;
 			pa_stream *stream;
 			pthread_mutex_t lock;
 			pthread_cond_t cond;
 			pthread_mutex_t lock2;
 			pthread_cond_t cond2;
 			int vol;
 			int end_stream;
 		} audio;
 	} song_ctx;
} __attribute__ ((packed)) music_player_t;

#endif /* GENERIC_H */