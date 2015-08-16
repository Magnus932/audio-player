
#ifndef GTK_H
#define GTK_H

#include <gtk/gtk.h>

#define IS_DIR(filename) ({\
	struct stat file_buf;\
	stat(filename, &file_buf);\
	(file_buf.st_mode & S_IFDIR) ? 1 : 0;\
})

GtkWidget *init_gtk(int *argc, char **argv);
void add_playlist(music_player_t *music);
void save_playlist(music_player_t *music);
void exit_nicely(music_player_t *music);
void exit_nicely_win_wrapper(GtkWindow *window,
							 GdkEvent *event,
							 gpointer user_data);
void exit_nicely_menu_wrapper(GtkMenuItem *menu_item,
							  gpointer user_data);
void recurse_dir(music_player_t *music, char *p);
void recurse_directories(music_player_t *music, GSList *list);
void set_volume(GtkRange *range, gpointer user_data);
void play_song(GtkToolButton *tool_button,
			   gpointer user_data);
void pause_song(GtkToolButton *tool_button,
				gpointer user_data);
void stop_song(GtkToolButton *tool_button,
			   gpointer user_data);
void play_audio_callback(GtkTreeView *tree_view, GtkTreePath *p,
						 GtkTreeViewColumn *column,
						 gpointer user_data);
void open_file(GtkMenuItem *menu_item, gpointer user_data);
void open_folder(GtkMenuItem *menu_item, gpointer user_data);
void tree_view_delete(GtkWidget *widget, GdkEvent *event,
					  gpointer user_data);
void setup_menu(music_player_t *music);
void setup_boxes(music_player_t *music);
void setup_toolbar(music_player_t *music);
void stop_progress_scale(music_player_t *music);
void *incr_progress_scale(void *user_data);
void start_progress_scale(music_player_t *music, guint max);
gboolean progress_scale_changed(GtkRange *range,
			   					GtkScrollType scroll,
								gdouble value,
								gpointer user_data);
gchar *progress_scale_fmt_value(GtkScale *scale, gdouble value,
			  				    gpointer user_data);
void setup_progress_scale(music_player_t *music);
void setup_tree_view(music_player_t *music);
void next_song(music_player_t *music);
gchar *get_duration(gchar *fname);

#endif /* GTK_H */