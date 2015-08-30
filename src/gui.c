
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <err.h>
#include <pwd.h>
#include "generic.h"

void setup_gui(music_player_t *music, int *argc,
			   char **argv)
{
	music->window = init_gtk(argc, argv);
	
	if (!music->window)
		errx(1, "Failed to initialize main window");
	g_signal_connect(G_OBJECT(music->window), "delete-event",
					 G_CALLBACK(exit_nicely_win_wrapper), music);
	setup_boxes(music);
	setup_menu(music);
	setup_toolbar(music);
	setup_progress_scale(music);
	setup_playlist_menu(music);
	setup_list_box(music);
	setup_tree_view(music);
	add_playlists(music);

	gtk_widget_show_all(music->window);
}

GtkWidget *init_gtk(int *argc, char **argv)
{
	GtkWidget *window;

	gtk_init(argc, &argv);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!window)
		return NULL;
	gtk_window_set_title(GTK_WINDOW(window), "ID04193W");
	gtk_window_set_default_size(GTK_WINDOW(window), 600, 600);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	return window;
}

void setup_boxes(music_player_t *music)
{
	music->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(music->window), music->vbox);

	music->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	music->hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
}

void activate_widgets(music_player_t *music)
{
	gtk_widget_set_sensitive(music->menu_item_open_file, TRUE);
	gtk_widget_set_sensitive(music->menu_item_open_folder, TRUE);
	gtk_widget_set_sensitive(music->menu_item_delete, TRUE);
}

void deactivate_widgets(music_player_t *music)
{
	gtk_widget_set_sensitive(music->menu_item_open_file, FALSE);
	gtk_widget_set_sensitive(music->menu_item_open_folder, FALSE);
	gtk_widget_set_sensitive(music->menu_item_delete, FALSE);
}

void select_playlist(GtkListBox *list_box, GtkListBoxRow *row,
					 gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	playlist_t *ptr;
	GtkWidget *label;
	const gchar *name;

	label = fetch_box_label(row);
	name = gtk_label_get_text(GTK_LABEL(label));

	ptr = music->playlists;
	while (ptr) {
		if (!strncmp(ptr->name, name, strlen(name)))
			break;
		ptr = ptr->next;
	}
	/*
	 * music->playlist always points to the active
	 * playlist.
	 */
	music->playlist = ptr;
	gtk_tree_view_set_model(GTK_TREE_VIEW(music->tree_view),
							GTK_TREE_MODEL(music->playlist->list));
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(music->tree_view),
									1);
}

gboolean list_box_menu(GtkWidget *widget, GdkEvent *event,
					   gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	GdkEventButton *button;

	button = (GdkEventButton *)&event->button;
	if (button->button != 3)
		return TRUE;
	gtk_menu_popup(GTK_MENU(music->list_box_menu), NULL, NULL,
				   NULL, NULL, 3, gtk_get_current_event_time());
	return TRUE;
}

void list_box_delete(GtkMenuItem *menu_item,
					 gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	playlist_t *ptr, *prev;
	GSList *p;
	GtkListBoxRow *row, *row2;
	GtkWidget *label;
	const gchar *name;
	guint num;

	row = gtk_list_box_get_selected_row(GTK_LIST_BOX(
										music->list_box));
	num = gtk_list_box_row_get_index(row);

	label = fetch_box_label(row);
	name = gtk_label_get_text(GTK_LABEL(label));

	ptr = music->playlists;
	while (ptr) {
		if (!strncmp(ptr->name, name, strlen(name))) {
			if (ptr == music->playlists) {
				music->playlists = ptr->next;
				if (music->playlists) {
					row2 = gtk_list_box_get_row_at_index(GTK_LIST_BOX(
														 music->list_box),
														 num + 1);
					gtk_tree_view_set_model(GTK_TREE_VIEW(music->tree_view),
											GTK_TREE_MODEL(music->playlists->list));
					music->playlist = music->playlists;
				}
				else
					deactivate_widgets(music);
			}
			else {
				prev->next = ptr->next;
				row2 = gtk_list_box_get_row_at_index(GTK_LIST_BOX(
													 music->list_box),
													 num - 1);
				gtk_tree_view_set_model(GTK_TREE_VIEW(music->tree_view),
										GTK_TREE_MODEL(prev->list));
				music->playlist = prev;
			}
			gtk_list_box_select_row(GTK_LIST_BOX(music->list_box), row2);
			p = ptr->paths;
			while (p) {
				g_free(p->data);
				p = p->next;
			}
			g_slist_free(ptr->paths);
			g_object_unref(ptr->list);
			free(ptr);
			break;
		}
		prev = ptr;
		ptr = ptr->next;
	}
	gtk_widget_destroy(GTK_WIDGET(row));
}

void setup_list_box_menu(music_player_t *music)
{
	music->list_box_menu = gtk_menu_new();

	music->menu_item_delete = gtk_menu_item_new_with_label("Delete");
	gtk_widget_set_sensitive(music->menu_item_delete, FALSE);
	g_signal_connect(G_OBJECT(music->menu_item_delete), "activate",
					 G_CALLBACK(list_box_delete), music);
	gtk_menu_shell_append(GTK_MENU_SHELL(music->list_box_menu),
						  music->menu_item_delete);
	
	gtk_widget_show_all(music->list_box_menu);
}

void setup_list_box(music_player_t *music)
{
	GtkWidget *sep;

	music->list_box = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(music->list_box),
									GTK_SELECTION_BROWSE);
	gtk_widget_set_size_request(music->list_box, 150, -1);
	g_signal_connect(G_OBJECT(music->list_box), "row-activated",
					 G_CALLBACK(select_playlist), music);
	g_signal_connect_after(G_OBJECT(music->list_box), "button-press-event",
					 	   G_CALLBACK(list_box_menu), music);
	gtk_box_pack_start(GTK_BOX(music->hbox1), music->list_box,
					   FALSE, FALSE, 0);
	setup_list_box_menu(music);

	sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(music->hbox1), sep, FALSE, FALSE, 0);
}

void cleanup_playlists(music_player_t *music)
{
	playlist_t *ptr, *ptr2;
	GSList *p;

	ptr = music->playlists;
	while (ptr) {
		p = ptr->paths;
		while (p) {
			g_free(p->data);
			p = p->next;
		}
		g_slist_free(ptr->paths);
		g_object_unref(ptr->list);
		ptr2 = ptr;
		ptr = ptr->next;
		free(ptr2);
	}
}

void exit_nicely(music_player_t *music)
{
	audio_stop_song(music);
	audio_close_connection(music);
	save_playlists(music);
	cleanup_playlists(music);
	gtk_widget_destroy(music->window);
	gtk_widget_destroy(music->list_box_menu);
	gtk_main_quit();

	pkt_queue_free(DECODER_PKT_QUEUE(music));
	free(music);
}

void exit_nicely_win_wrapper(GtkWindow *window,
							 GdkEvent *event,
				             gpointer user_data)
{
	exit_nicely((music_player_t *)user_data);
}

void exit_nicely_menu_wrapper(GtkMenuItem *menu_item,
							  gpointer user_data)
{
	exit_nicely((music_player_t *)user_data);
}

void open_file(GtkMenuItem *menu_item, gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	playlist_t *playlist = music->playlist;
	GtkWidget *dialog;
	GtkTreeIter iter;
	GtkFileChooser *chooser;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	gint retval;
	GSList *filenames, *ptr;

	dialog = gtk_file_chooser_dialog_new("Open File", GTK_WINDOW(music->window),
										 action, "Open", GTK_RESPONSE_ACCEPT,
										 "Cancel", GTK_RESPONSE_CANCEL, NULL);
	chooser = GTK_FILE_CHOOSER(dialog);
	gtk_file_chooser_set_select_multiple(chooser, TRUE);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));

	if (retval == GTK_RESPONSE_ACCEPT) {
		filenames = gtk_file_chooser_get_filenames(chooser), ptr = filenames;
		/*
		 * Getting the duration for each song while
		 * actually putting them into the GtkTreeView
		 * causes a huge delay on the GUI. We use music->paths_len
		 * to assign a start location for a thread that will
		 * get the duration after they have been added into
		 * the playlist.
		 */
		if (playlist->paths)
			playlist->paths_len = g_slist_length(playlist->paths);
		else
			playlist->paths_len = 0;

		pthread_mutex_lock(&playlist->mutex);
		while (ptr) {
			if (!is_dir((gchar *)ptr->data)) {
				/*
				 * Save a reference to the path itself.
				 */
				playlist->paths = g_slist_insert(playlist->paths, ptr->data,
												 playlist->song_cnt);
				gtk_list_store_append(playlist->list, &iter);
				gtk_list_store_set(playlist->list, &iter, 0, ++playlist->song_cnt,
							   	   1, song_name((gchar *)ptr->data), -1);
			}
			ptr = ptr->next;
		}
		pthread_mutex_unlock(&playlist->mutex);
		g_slist_free(filenames);

		if (!duration_thread_running(playlist))
			schedule_duration_thread(playlist);
	}
	gtk_widget_destroy(dialog);
}

void recurse_dir(playlist_t *playlist, char *p)
{
	DIR *dirp;
	struct dirent *entry;
	struct stat file_buf;
	GtkTreeIter iter;
	gchar pbuf[4000];
	gchar *path;

	dirp = opendir(p);
	while ( (entry = readdir(dirp))) {
		if (!strncmp(entry->d_name, "..", 2) || (!strncmp(entry->d_name, ".", 1)))
			continue;
		snprintf(pbuf, 3999, "%s/%s", p, entry->d_name);
		stat(pbuf, &file_buf);
		if (file_buf.st_mode & S_IFDIR)
			recurse_dir(playlist, pbuf);
		else if (file_buf.st_mode & S_IFREG) {
			/*
			 * g_slist_insert will not make a copy of the
			 * data passed, so we need to allocate some memory
			 * for the song path on the heap before calling it.
			 */
			path = g_malloc(strlen(pbuf) + 1);
			strncpy(path, pbuf, strlen(pbuf));
			path[strlen(pbuf)] = '\0';
			playlist->paths = g_slist_insert(playlist->paths, (gpointer)path,
										     playlist->song_cnt);
			gtk_list_store_append(playlist->list, &iter);
			gtk_list_store_set(playlist->list, &iter, 0, ++playlist->song_cnt,
							   1, song_name(pbuf), -1);
		}
	}
	closedir(dirp);
}

void recurse_directories(playlist_t *playlist, GSList *list)
{
	/*
	 * Algorithm recurses every directory from the
	 * root directories in GSList list, and adds
	 * every file it finds into the tree_view model.
	 * Every directory and file should be valid because
	 * of the dialog, so no error checking is required.
	 */
	GSList *ptr = list;

	while (ptr) {
		recurse_dir(playlist, (char *)ptr->data);
		g_free(ptr->data);
		ptr = ptr->next;
	}
}

void open_folder(GtkMenuItem *menu_item, gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	playlist_t *playlist = music->playlist;
	GtkWidget *dialog;
	GtkFileChooser *chooser;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
	gint retval;
	GSList *filenames;

	dialog = gtk_file_chooser_dialog_new("Open Folder", GTK_WINDOW(music->window),
										 action, "Open", GTK_RESPONSE_ACCEPT,
										 "Cancel", GTK_RESPONSE_CANCEL, NULL);
	chooser = GTK_FILE_CHOOSER(dialog);
	gtk_file_chooser_set_select_multiple(chooser, TRUE);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));
	if (retval == GTK_RESPONSE_ACCEPT) {
		filenames = gtk_file_chooser_get_filenames(chooser);
		/*
		 * Getting the duration for each song while
		 * actually putting them into the GtkTreeView
		 * causes a huge delay on the GUI. We use music->paths_len
		 * to assign a start location for a thread that will
		 * get the duration after they have been added into
		 * the playlist.
		 */
		if (playlist->paths)
			playlist->paths_len = g_slist_length(playlist->paths);
		else
			playlist->paths_len = 0;

		pthread_mutex_lock(&playlist->mutex);
		recurse_directories(playlist, filenames);
		pthread_mutex_unlock(&playlist->mutex);
		g_slist_free(filenames);

		if (!duration_thread_running(playlist))
			schedule_duration_thread(playlist);
	}
	gtk_widget_destroy(dialog);
}

void setup_menu(music_player_t *music)
{
	GtkWidget *box1, *box2, *box3;
	GtkWidget *icon1, *icon2, *icon3;
	GtkWidget *label1, *label2, *label3;

	box1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	icon1 = gtk_image_new_from_icon_name("document-open-symbolic", GTK_ICON_SIZE_MENU);
	gtk_container_add(GTK_CONTAINER(box1), icon1);
	label1 = gtk_label_new("Open File");
	gtk_container_add(GTK_CONTAINER(box1), label1);
	music->menu_item_open_file = gtk_menu_item_new();
	gtk_widget_set_sensitive(music->menu_item_open_file, FALSE);
	gtk_container_add(GTK_CONTAINER(music->menu_item_open_file), box1);

	box2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	icon2 = gtk_image_new_from_icon_name("folder-symbolic", GTK_ICON_SIZE_MENU);
	gtk_container_add(GTK_CONTAINER(box2), icon2);
	label2 = gtk_label_new("Open Folder");
	gtk_container_add(GTK_CONTAINER(box2), label2);
	music->menu_item_open_folder = gtk_menu_item_new();
	gtk_widget_set_sensitive(music->menu_item_open_folder, FALSE);
	gtk_container_add(GTK_CONTAINER(music->menu_item_open_folder), box2);

	box3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	icon3 = gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
	gtk_container_add(GTK_CONTAINER(box3), icon3);
	label3 = gtk_label_new("Close");
	gtk_container_add(GTK_CONTAINER(box3), label3);
	music->menu_item_close = gtk_menu_item_new();
	gtk_container_add(GTK_CONTAINER(music->menu_item_close), box3);

	music->menu_item_file = gtk_menu_item_new_with_label("File");
	music->menu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(music->menu_item_file),
							  music->menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(music->menu),
						  music->menu_item_open_file);
	gtk_menu_shell_append(GTK_MENU_SHELL(music->menu),
						  music->menu_item_open_folder);
	music->menu_item_separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(music->menu),
						  music->menu_item_separator);
	gtk_menu_shell_append(GTK_MENU_SHELL(music->menu),
						  music->menu_item_close);
	music->menu_bar = gtk_menu_bar_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(music->menu_bar), music->menu_item_file);

	g_signal_connect(G_OBJECT(music->menu_item_open_file), "activate",
					 G_CALLBACK(open_file), music);
	g_signal_connect(G_OBJECT(music->menu_item_open_folder), "activate",
					 G_CALLBACK(open_folder), music);
	g_signal_connect(G_OBJECT(music->menu_item_close), "activate",
					 G_CALLBACK(exit_nicely_menu_wrapper), music);
	
	gtk_box_pack_start(GTK_BOX(music->vbox), music->menu_bar, FALSE, FALSE, 0);
}

void play_song(GtkTreeView *tree_view, GtkTreePath *p,
			   GtkTreeViewColumn *column,
			   gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	playlist_t *playlist;
	guint num;

	playlist = music->playlist;

	uncolorize_last_song(playlist);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(playlist->list),
							&playlist->iter, p);
	num = tree_view_row(GTK_TREE_MODEL(playlist->list), &playlist->iter);
	if (!decode_audio_file(music, (gchar *)g_slist_nth_data(playlist->paths, num)))
		stop_progress_scale(music);
	colorize_song_playing(playlist);
}

void next_song(music_player_t *music)
{
	playlist_t *playlist;
	gchar *filename;
	guint num, retval;

	playlist = music->playlist;
	if (music->repeat) {
		num = tree_view_row(GTK_TREE_MODEL(playlist->list), &playlist->iter);
		filename = (gchar *)g_slist_nth_data(playlist->paths, num);
		decode_audio_file(music, filename);
		return;
	}
	uncolorize_last_song(playlist);
	do { 
		retval = gtk_tree_model_iter_next(GTK_TREE_MODEL(playlist->list),
									  	  &playlist->iter);
		if (!retval) {
			stop_progress_scale(music);
			return;
		}
		num = tree_view_row(GTK_TREE_MODEL(playlist->list), &playlist->iter);
		filename = (gchar *)g_slist_nth_data(playlist->paths, num);
	} while (!decode_audio_file(music, filename));
	colorize_song_playing(playlist);
}

void resume_song(GtkToolButton *tool_button,
				 gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;

	audio_resume_song(music);
}

void pause_song(GtkToolButton *tool_button,
				gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;

	audio_pause_song(music);
}

void stop_song(GtkToolButton *tool_button,
			   gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	
	stop_progress_scale(music);
	audio_stop_song(music);
}

void repeat_song(GtkToolButton *tool_button,
				 gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	GdkRGBA color;
	GtkWidget *child;
	
	if (music->repeat) {
		gdk_rgba_parse(&color, "black");
		music->repeat = 0;
	}
	else {
		gdk_rgba_parse(&color, "orange");
		music->repeat = 1;
	}
	child = gtk_bin_get_child(GTK_BIN(music->tool_item_repeat));
	gtk_widget_override_color(child, GTK_STATE_NORMAL,
							  &color);
}

void set_volume(GtkRange *range, gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;

	AUDIO_VOL(music) = gtk_range_get_value(range);
	audio_set_volume(music);
}

gchar *volume_scale_fmt_value(GtkScale *scale, gdouble value,
							  gpointer user_data)
{
	int percentage;

	percentage = (value / (gdouble)PA_VOLUME_NORM) * 100;
	return g_strdup_printf("%d%%", percentage);
}

void setup_toolbar(music_player_t *music)
{
	music->toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(music->toolbar), GTK_TOOLBAR_ICONS);

	music->tool_item_play = gtk_tool_button_new(NULL, NULL);
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(music->tool_item_play),
								  "media-playback-start-symbolic");
	gtk_toolbar_insert(GTK_TOOLBAR(music->toolbar), music->tool_item_play,
					   -1);
	g_signal_connect(G_OBJECT(music->tool_item_play), "clicked",
					 G_CALLBACK(resume_song), music);

	music->tool_item_pause = gtk_tool_button_new(NULL, NULL);
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(music->tool_item_pause),
								  "media-playback-pause-symbolic");
	gtk_toolbar_insert(GTK_TOOLBAR(music->toolbar), music->tool_item_pause,
					   -1);
	g_signal_connect(G_OBJECT(music->tool_item_pause), "clicked",
					 G_CALLBACK(pause_song), music);

	music->tool_item_stop = gtk_tool_button_new(NULL, NULL);
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(music->tool_item_stop),
								  "media-playback-stop-symbolic");
	gtk_toolbar_insert(GTK_TOOLBAR(music->toolbar), music->tool_item_stop,
					   -1);
	g_signal_connect(G_OBJECT(music->tool_item_stop), "clicked",
					 G_CALLBACK(stop_song), music);

	music->tool_item_repeat = gtk_tool_button_new(NULL, NULL);
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(music->tool_item_repeat),
								  "media-playlist-repeat-symbolic");
	gtk_toolbar_insert(GTK_TOOLBAR(music->toolbar), music->tool_item_repeat,
								   -1);
	g_signal_connect(G_OBJECT(music->tool_item_repeat), "clicked",
					 G_CALLBACK(repeat_song), music);

	music->tool_item_separator = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(music->toolbar), music->tool_item_separator, -1);

	music->tool_item_volume = gtk_tool_item_new();
	music->volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
												   0, PA_VOLUME_NORM, 1.0);
	gtk_scale_set_value_pos(GTK_SCALE(music->volume_scale), GTK_POS_LEFT);
	gtk_range_set_value(GTK_RANGE(music->volume_scale), PA_VOLUME_NORM);
	gtk_widget_set_size_request(music->volume_scale, 250, -1);
	gtk_container_add(GTK_CONTAINER(music->tool_item_volume), music->volume_scale);
	gtk_toolbar_insert(GTK_TOOLBAR(music->toolbar), music->tool_item_volume, -1);
	g_signal_connect(G_OBJECT(music->volume_scale), "value-changed",
					 G_CALLBACK(set_volume), music);
	g_signal_connect(G_OBJECT(music->volume_scale), "format-value",
					 G_CALLBACK(volume_scale_fmt_value), NULL);
	/*
	 * Lastly pack the toolbar into the vbox.
	 */
	gtk_box_pack_start(GTK_BOX(music->vbox), music->toolbar, FALSE, FALSE, 0);
}

void start_progress_scale(music_player_t *music)
{
	pthread_create(&music->tid, NULL, incr_progress_scale,
				   music);
}

/*
 * This will be called every time a new song
 * is ready for playback to restart the scale.
 */
void restart_progress_scale(music_player_t *music,
							guint duration)
{
	gtk_range_set_range(GTK_RANGE(music->progress_scale), 0,
						duration);
	gtk_range_set_value(GTK_RANGE(music->progress_scale), 0);

	music->seconds = 0;
}

void resume_progress_scale(music_player_t *music)
{
	music->pause = 0;
	pthread_cond_signal(&music->cond);
}

void pause_progress_scale(music_player_t *music)
{
	music->pause = 1;
}

void stop_progress_scale(music_player_t *music)
{
	music->pause = 0;
	pthread_cond_signal(&music->cond);
	if (music->tid) {
		pthread_cancel(music->tid);
		music->tid = 0;
	}
	gtk_range_set_range(GTK_RANGE(music->progress_scale), 0, 0);
}

void *incr_progress_scale(void *user_data)
{
	music_player_t *music = (music_player_t *)user_data;

	while (1) {
		if (music->pause) {
			pthread_mutex_lock(&music->mutex);
			pthread_cond_wait(&music->cond, &music->mutex);
			pthread_mutex_unlock(&music->mutex);
		}
		pthread_testcancel();
		gtk_range_set_value(GTK_RANGE(music->progress_scale),
							music->seconds);
		music->seconds++;
		sleep(1);
	}
}

gboolean progress_scale_changed(GtkRange *range,
			   					GtkScrollType scroll,
								gdouble value,
								gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	long val;

	if (!DECODER_CTX(music)) return FALSE;
	val = value;

	decoder_update_frame(music, val, (val >= music->seconds)
						 ? 0 : 1);
	music->seconds = val;
	usleep(1000 * 50);

	return FALSE;
}

gchar *progress_scale_fmt_value(GtkScale *scale, gdouble value,
			  				    gpointer user_data)
{
	int minutes = (int)value / 60;
	int seconds = (int)value % 60;

	return g_strdup_printf("%s%d:%s%d", (minutes < 10) ? "0" : "",
						   minutes, (seconds < 10) ? "0" : "", seconds); 
}

void setup_progress_scale(music_player_t *music)
{
	music->progress_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL,
										  NULL);
	gtk_range_set_range(GTK_RANGE(music->progress_scale), 0.0, 0.0);
	gtk_scale_set_value_pos(GTK_SCALE(music->progress_scale), GTK_POS_LEFT);
	g_signal_connect(G_OBJECT(music->progress_scale), "change-value",
					 G_CALLBACK(progress_scale_changed), music);
	g_signal_connect(G_OBJECT(music->progress_scale), "format-value",
					 G_CALLBACK(progress_scale_fmt_value), NULL);
	gtk_box_pack_start(GTK_BOX(music->vbox), music->progress_scale,
					   FALSE, FALSE, 0);
}

void new_playlist(GtkWidget *widget, gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	playlist_t *ptr, *entry;
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *image;
	GtkWidget *label;
	const gchar *name;

	if (!gtk_entry_get_text_length(GTK_ENTRY(music->entry)))
		return;
	name = gtk_entry_get_text(GTK_ENTRY(music->entry));

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	image = gtk_image_new_from_icon_name("audio-x-generic",
										 GTK_ICON_SIZE_MENU);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	label = gtk_label_new(name);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

	row = gtk_list_box_row_new();
	gtk_container_add(GTK_CONTAINER(row), box);
	gtk_widget_show_all(row);
	gtk_list_box_insert(GTK_LIST_BOX(music->list_box), row, -1);
	gtk_list_box_select_row(GTK_LIST_BOX(music->list_box),
							GTK_LIST_BOX_ROW(row));
	/*
	 * Every time we add a new playlist, concentate a new
	 * playlist_t datatype onto the tail of the list and
	 * fill it out.
	 */
	entry = (playlist_t *)malloc(sizeof(playlist_t));
	memset(entry, '\0', sizeof(playlist_t));
	strncpy(entry->name, name, strlen(name) + 1);
	entry->list = gtk_list_store_new(4, G_TYPE_INT, G_TYPE_STRING,
									 G_TYPE_STRING, G_TYPE_STRING);
	pthread_mutex_init(&entry->mutex, NULL);
	entry->next = NULL;

	ptr = music->playlists;
	if (ptr) {
		while (ptr->next) ptr = ptr->next;
		ptr->next = entry;
	}
	else {
		music->playlists = entry;
		activate_widgets(music);
	}
	music->playlist = entry;
	gtk_tree_view_set_model(GTK_TREE_VIEW(music->tree_view),
							GTK_TREE_MODEL(music->playlist->list));
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(music->tree_view),
									1);
	gtk_entry_set_text(GTK_ENTRY(music->entry), "");
}

playlist_t *add_playlist(music_player_t *music, char *name)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *image;
	GtkWidget *label;
	playlist_t *entry, *ptr;
	static guint i = 0;

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	image = gtk_image_new_from_icon_name("audio-x-generic", GTK_ICON_SIZE_MENU);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	label = gtk_label_new(name);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

	row = gtk_list_box_row_new();
	gtk_container_add(GTK_CONTAINER(row), box);
	gtk_list_box_insert(GTK_LIST_BOX(music->list_box), row, -1);
	gtk_widget_show_all(row);
	
	entry = (playlist_t *)malloc(sizeof(playlist_t));
	memset(entry, '\0', sizeof(playlist_t));
	strncpy(entry->name, name, strlen(name) + 1);
	entry->list = gtk_list_store_new(4, G_TYPE_INT, G_TYPE_STRING,
									 G_TYPE_STRING, G_TYPE_STRING);
	pthread_mutex_init(&entry->mutex, NULL);
	entry->next = NULL;

	ptr = music->playlists;
	if (ptr) {
		while (ptr->next) ptr = ptr->next;
		ptr->next = entry;
	}
	else
		music->playlists = entry;
	if (!i++) {
		music->playlist = entry;
		gtk_list_box_select_row(GTK_LIST_BOX(music->list_box),
								GTK_LIST_BOX_ROW(row));
		gtk_tree_view_set_model(GTK_TREE_VIEW(music->tree_view),
								GTK_TREE_MODEL(music->playlist->list));
		gtk_tree_view_set_search_column(GTK_TREE_VIEW(music->tree_view),
										1);
		activate_widgets(music);
	}
	return entry;
}

void add_playlists(music_player_t *music)
{
	struct passwd *p;
	int fd;
	char *buf;
	char *ptr, *ptr2;
	struct stat file_buf;
	GtkTreeIter iter;
	playlist_t *playlist;
	gchar *path;

	p = getpwuid(getuid());
	if (!p)
		return;
	buf = (char *)malloc(strlen(p->pw_dir) + 8);
	snprintf(buf, strlen(p->pw_dir) + 8, "%s/.audio", p->pw_dir);

	fd = open(buf, O_RDONLY, 0);
	free(buf);
	if (fd == -1)
		return;
	if (fstat(fd, &file_buf) == -1)
		close(fd);

	buf = (char *)malloc(sizeof(char) * file_buf.st_size);
	read(fd, buf, file_buf.st_size);
		
	for (ptr = buf; *ptr != '\0'; ptr++) {
		if (!strncmp(ptr, "[PLAYLIST", 9)) {
			ptr += 10;
			for (ptr2 = ptr; *ptr2 != ']'; ptr2++);
			*ptr2 = '\0';
			playlist = add_playlist(music, ptr);

			ptr = ptr2 + 1;
			continue;
		}
		/*
		 * This code is only generic for the current playlist added.
		 * To make it easier to take input, every song in a
		 * playlist container is prefixed with ".". So in order
		 * to find out if we are working on a new song to add;
		 * if (*ptr == '.' && *(ptr - 1) == '\n') will satisfy.
		 */
		if (*ptr == '.' && *(ptr - 1) == '\n') {
			ptr++;
			for (ptr2 = ptr; ; ptr2++)
				if (!strncmp(ptr2, "<<0x41424344>>", 14))
					break;
			*ptr2 = '\0';
			path = g_malloc(strlen(ptr) + 1);
			strncpy(path, ptr, strlen(ptr) + 1);
			playlist->paths = g_slist_insert(playlist->paths, path,
											 playlist->song_cnt);
			gtk_list_store_append(playlist->list, &iter);
			gtk_list_store_set(playlist->list, &iter, 0, ++playlist->song_cnt,
							   1, song_name(path), -1);
			ptr = ptr2 + 14;
			if (*ptr != '\n') {
				for (ptr2 = ptr; *ptr2 != '\n'; ptr2++);
				*ptr2 = '\0';
				gtk_list_store_set(playlist->list, &iter, 2, ptr, -1);
				*ptr2 = '\n';
				ptr = ptr2;
			}
		}
	}
	free(buf);
	close(fd);
}

void save_playlists(music_player_t *music)
{
	struct passwd *p;
	int fd;
	char *buf;
	char *duration;
	char obuf[4000];
	GtkTreeIter iter;
	GSList *list_ptr;
	playlist_t *ptr;
	gchar *path;

	p = getpwuid(getuid());
	if (!p)
		return;
	buf = (char *)malloc(strlen(p->pw_dir) + 8);
	snprintf(buf, strlen(p->pw_dir) + 8, "%s/.audio", p->pw_dir);
	fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR);
	free(buf);

	ptr = music->playlists;
	while (ptr) {
		snprintf(obuf, 3999, "[PLAYLIST %s]\n", ptr->name);
		write(fd, obuf, strlen(obuf));

		gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ptr->list),
									  &iter);
		list_ptr = ptr->paths;
		while (list_ptr) {
			path = (gchar *)list_ptr->data;
			gtk_tree_model_get(GTK_TREE_MODEL(ptr->list), &iter,
							   2, &duration, -1);
			snprintf(obuf, 3999, ".%s<<0x41424344>>%s\n", path, (duration)
					 ? duration : "");
			write(fd, obuf, strlen(obuf));
			g_free(duration);

			gtk_tree_model_iter_next(GTK_TREE_MODEL(ptr->list), &iter);
			list_ptr = list_ptr->next;
		}
		ptr = ptr->next;
	}
	close(fd);
}

void is_duration_added(music_player_t *music)
{
	playlist_t *playlist;
	gchar *filename, *duration;
	guint num;

	playlist = music->playlist;
	gtk_tree_model_get(GTK_TREE_MODEL(playlist->list), &playlist->iter,
					   2, &duration, -1);
	if (!duration) {
		num = tree_view_row(GTK_TREE_MODEL(playlist->list),
							&playlist->iter);
		filename = (gchar *)g_slist_nth_data(playlist->paths, num);

		duration = get_duration(filename);
		gtk_list_store_set(playlist->list, &playlist->iter, 2,
						   duration, -1);		
		g_free(duration);
	}
}

gchar *get_duration(gchar *fname)
{
	int duration;
	int minutes, seconds;
	gchar *buf;

	duration = decoder_get_duration(fname);
	if (!duration)
		return NULL;
	buf = g_malloc(sizeof(gchar) * 20);

	minutes = duration / 60;
	seconds = duration % 60;
	snprintf(buf, 19, "%s%d:%s%d", (minutes < 10) ? "0" : "",
			 minutes, (seconds < 10) ? "0" : "", seconds);
	return buf;
} 

gint duration_thread_running(playlist_t *playlist)
{
	return (playlist->tid) ? 1 : 0; 
}

void *duration_thread(void *user_data)
{
	playlist_t *playlist = (playlist_t *)user_data;
	gchar *duration, buf[10];
	GtkTreeIter iter;
	GSList *ptr;

	snprintf(buf, 9, "%d", playlist->paths_len);
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(playlist->list),
									    &iter, buf);
	ptr = g_slist_nth(playlist->paths, playlist->paths_len);
	while (ptr) {
		duration = get_duration((gchar *)ptr->data);
		if (duration) {
			gtk_list_store_set(playlist->list, &iter, 2, duration, -1);
			g_free(duration);
		}
		gtk_tree_model_iter_next(GTK_TREE_MODEL(playlist->list), &iter);

		pthread_mutex_lock(&playlist->mutex);
		ptr = ptr->next;
		pthread_mutex_unlock(&playlist->mutex);
	}
	playlist->tid = 0;
}

void schedule_duration_thread(playlist_t *playlist)
{
	pthread_create(&playlist->tid, NULL, duration_thread,
				   playlist);
}

void setup_playlist_menu(music_player_t *music)
{
	GtkWidget *label;
	GtkWidget *sep, *sep2;

	sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(music->vbox), sep, FALSE, FALSE, 4);

	label = gtk_label_new("Playlists");
	gtk_box_pack_start(GTK_BOX(music->hbox), label, FALSE, FALSE, 4);

	/*
	 * Due to using gtk_box_pack_end, we will reverse
	 * setup the widgets and pack them.
	 */
	music->button = gtk_button_new_with_label("Add");
	g_signal_connect(G_OBJECT(music->button), "clicked",
					 G_CALLBACK(new_playlist), music);
	gtk_box_pack_end(GTK_BOX(music->hbox), music->button, FALSE,
					 FALSE, 4);

	music->entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(music->entry), "Playlist name");
	gtk_entry_set_max_length(GTK_ENTRY(music->entry), 19);
	g_signal_connect(G_OBJECT(music->entry), "activate",
					 G_CALLBACK(new_playlist), music);
	gtk_box_pack_end(GTK_BOX(music->hbox), music->entry, FALSE,
					 FALSE, 0);

	gtk_box_pack_start(GTK_BOX(music->vbox), music->hbox, FALSE,
					   FALSE, 4);
	sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(music->vbox), sep2, FALSE, FALSE,
					   0);
}

void uncolorize_last_song(playlist_t *playlist)
{
	if (!gtk_list_store_iter_is_valid(playlist->list, &playlist->iter))
		return;
	gtk_list_store_set(playlist->list, &playlist->iter, 3,
					   "white", -1);
}

void colorize_song_playing(playlist_t *playlist)
{
	gtk_list_store_set(playlist->list, &playlist->iter, 3, "gray",
					   -1);
}

gboolean tree_view_search(GtkTreeModel *model, gint column,
						  const gchar *key, GtkTreeIter *iter,
						  gpointer user_data)
{
	gchar *string;

	gtk_tree_model_get(model, iter, column, &string, -1);
	if (strcasestr(string, key)) {
		g_free(string);
		return FALSE;
	}
	g_free(string);
	return TRUE;
}

void tree_view_delete(GtkWidget *widget, GdkEvent *event,
					  gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	playlist_t *playlist = music->playlist;
	GtkTreeSelection *selection;
	GtkTreePath *p;
	GList *list, *ptr;
	GtkTreeIter iter;
	GtkTreeRowReference **rows;
	guint num_rows, num, i = 0;
	gchar *path;
	GSList *entry;

	if (((GdkEventKey *)event)->keyval != GDK_KEY_Delete)
		return;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(music->tree_view));
	num_rows = gtk_tree_selection_count_selected_rows(selection);
	if (!num_rows)
		return;
	list = gtk_tree_selection_get_selected_rows(selection, (GtkTreeModel **)&playlist->list);
	rows = (GtkTreeRowReference **)g_malloc(sizeof(GtkTreeRowReference *)
										    * num_rows);
	ptr = list;
	while (ptr) {
		rows[i++] = gtk_tree_row_reference_new(GTK_TREE_MODEL(playlist->list),
				   							   (GtkTreePath *)ptr->data);
		ptr = ptr->next;
	}
	g_list_free_full(list, (GDestroyNotify)gtk_tree_path_free);
	for (i = 0; i < num_rows; i++) {
		p = gtk_tree_row_reference_get_path(rows[i]);
		gtk_tree_model_get_iter(GTK_TREE_MODEL(playlist->list), &iter, p);
		gtk_tree_row_reference_free(rows[i]);
		gtk_tree_path_free(p);
		
		entry = g_slist_nth(playlist->paths,
							tree_view_row(GTK_TREE_MODEL(playlist->list),
													     &iter));
		g_free(entry->data);
		playlist->paths = g_slist_delete_link(playlist->paths, entry);
		gtk_list_store_remove(playlist->list, &iter);
	}
	g_free(rows);
}

void setup_tree_view(music_player_t *music)
{
	GtkTreeSelection *selection;

	music->scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(music->scrolled_window),
								   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	music->tree_view = gtk_tree_view_new();
	gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(music->tree_view),
										tree_view_search, music, NULL);
	
	g_signal_connect(G_OBJECT(music->tree_view), "row-activated",
					 G_CALLBACK(play_song), music);
	g_signal_connect_after(G_OBJECT(music->tree_view), "key-press-event",
						   G_CALLBACK(tree_view_delete), music);
	gtk_container_add(GTK_CONTAINER(music->scrolled_window), music->tree_view);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(music->tree_view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

	music->num_song = gtk_tree_view_column_new();
	music->num_rend = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(music->num_song, music->num_rend, FALSE);
	gtk_tree_view_column_set_attributes(music->num_song, music->num_rend,
										"text", 0, "cell-background", 3,
										NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(music->tree_view), music->num_song);

	music->song = gtk_tree_view_column_new();
	music->song_rend = gtk_cell_renderer_text_new();
	gtk_tree_view_column_set_title(music->song, "Song");
	gtk_tree_view_column_pack_start(music->song, music->song_rend, FALSE);
	gtk_tree_view_column_set_attributes(music->song, music->song_rend,
										"text", 1, "cell-background", 3,
										NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(music->tree_view), music->song);

	music->dur = gtk_tree_view_column_new();
	music->dur_rend = gtk_cell_renderer_text_new();
	gtk_tree_view_column_set_title(music->dur, "Duration");
	gtk_tree_view_column_pack_start(music->dur, music->dur_rend, FALSE);
	gtk_tree_view_column_set_attributes(music->dur, music->dur_rend,
										"text", 2, "cell-background", 3,
										NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(music->tree_view), music->dur);

	gtk_box_pack_start(GTK_BOX(music->hbox1), music->scrolled_window, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(music->vbox), music->hbox1,
					   TRUE, TRUE, 0);
}