
#ifndef GTK_H
#define GTK_H

#include <gtk/gtk.h>

/*
 * Datatype represents a playlist. A playlist
 * contains an identifier which is the name, a single
 * linked list, and a GtkListStore which is used as a model
 * for the GtkTreeView. Only the head is saved in the context
 * structure, because iterating over a few structures does no harm.

 * A mapping between the identifier label
 * in a GtkListBoxRow and the name in this datatype is used
 * to change the playlist.
 *
 * Concerning the pthread member, it is used to get the duration
 * of songs that belongs to the playlist context in question.
 * The AVCodec library requires "allot" (in fact its so slow that
 * it should be replaced with another mechanism) to get the duration
 * of a stream, so this needs to be padded into the GtkTreeModel
 * in a different thread than the GTK thread.
 * The mutex is used to block execution
 * if requesting more files into the playlist while the thread
 * is running, instead of restarting the thread with a new offset.
 */
typedef struct playlist {
	gchar name[20];
	GSList *paths;
	guint paths_len;
	guint song_cnt;
	GtkListStore *list;
	GtkTreeIter iter;
	pthread_t tid;
	pthread_mutex_t mutex;
	struct playlist *next;
} playlist_t;

#define is_dir(filename) ({\
	struct stat file_buf;\
	stat(filename, &file_buf);\
	(file_buf.st_mode & S_IFDIR) ? 1 : 0;\
})
#define song_name(path) ({\
	char *p, *retval;\
	for (p = path; *p != '\0'; p++)\
		if (*p == '/')\
			retval = p;\
	retval + 1;\
})

/*
 * A mapping between the full path that resides in
 * music->paths ( GSList linked list ) and this macro
 * will be used. There is probably a better way ;).
 */
#define tree_view_row(list, iter) ({\
	int retval;\
	char *str;\
	str = gtk_tree_model_get_string_from_iter(list, iter);\
	retval = atoi(str);\
	g_free(str);\
	retval;\
})
#define fetch_box_label(list_box_row) ({\
	GtkWidget *box;\
	GtkWidget *retval;\
	GList *list;\
	box = gtk_bin_get_child(GTK_BIN(list_box_row));\
	list = gtk_container_get_children(GTK_CONTAINER(box));\
	while (list) {\
		retval = (GtkWidget *)list->data;\
		if (GTK_IS_LABEL(retval))\
			break;\
		list = list->next;\
	}\
	g_list_free(list);\
	retval;\
})

void open_file(GtkMenuItem *menu_item, gpointer user_data);
void recurse_dir(playlist_t *playlist, char *p);
void recurse_directories(playlist_t *playlist, GSList *list);
void open_folder(GtkMenuItem *menu_item, gpointer user_data);

void play_song(GtkTreeView *tree_view, GtkTreePath *p,
			   GtkTreeViewColumn *column,
			   gpointer user_data);
void next_song(music_player_t *music);
void pause_song(GtkToolButton *tool_button,
				gpointer user_data);
void stop_song(GtkToolButton *tool_button,
			   gpointer user_data);
void resume_song(GtkToolButton *tool_button,
			     gpointer user_data);

void set_volume(GtkRange *range, gpointer user_data);
gchar *volume_scale_fmt_value(GtkScale *scale, gdouble value,
							  gpointer user_data);

gchar *get_duration(gchar *fname);
void is_duration_added(music_player_t *music);

void start_progress_scale(music_player_t *music);
void pause_progress_scale(music_player_t *music);
void stop_progress_scale(music_player_t *music);
void resume_progress_scale(music_player_t *music);
void restart_progress_scale(music_player_t *music,
							guint duration);
void *incr_progress_scale(void *user_data);
gboolean progress_scale_changed(GtkRange *range,
			   					GtkScrollType scroll,
								gdouble value,
								gpointer user_data);
gchar *progress_scale_fmt_value(GtkScale *scale, gdouble value,
			  				    gpointer user_data);

void schedule_duration_thread(playlist_t *playlist);
gint duration_thread_running(playlist_t *playlist);
void *duration_thread(void *user_data);

GtkWidget *init_gtk(int *argc, char **argv);
void setup_gui(music_player_t *music, int *argc,
			   char **argv);
void setup_menu(music_player_t *music);
void setup_boxes(music_player_t *music);
void setup_toolbar(music_player_t *music);
void setup_progress_scale(music_player_t *music);
void setup_playlist_menu(music_player_t *music);
void setup_list_box(music_player_t *music);
void setup_tree_view(music_player_t *music);
void exit_nicely(music_player_t *music);
void exit_nicely_win_wrapper(GtkWindow *window,
							 GdkEvent *event,
							 gpointer user_data);
void exit_nicely_menu_wrapper(GtkMenuItem *menu_item,
							  gpointer user_data);
void colorize_song_playing(playlist_t *music);
void uncolorize_last_song(playlist_t *music);

playlist_t *add_playlist(music_player_t *music, char *name);
void add_playlists(music_player_t *music);
void save_playlists(music_player_t *music);

gboolean tree_view_search(GtkTreeModel *model, gint column,
						  const gchar *key, GtkTreeIter *iter,
						  gpointer user_data);
void tree_view_delete(GtkWidget *widget, GdkEvent *event,
					  gpointer user_data);

#endif /* GTK_H */