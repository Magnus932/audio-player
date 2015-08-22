
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

/*
 * The code of gui.c is divided into two segments.
 * The first segment contains callbacks and everything
 * related to playing a song.
 *
 * The second segment contains all the code that is needed
 * to set up the gui with the appropiate widgets.
 */

/*
 * Callback & misc area;
 */
void exit_nicely(music_player_t *music)
{
	audio_stop_song(music);
	audio_close_connection(music);
	save_playlist(music);
	gtk_widget_destroy(music->window);
	/*
	 * The tree_view menu is not attached to anything
	 * when idle. Destroying the main window will not
	 * free the resources. Simply free it here.
	 */
	gtk_widget_destroy(music->tree_view_menu);
	/*
	 * After destroying the GtkTreeView, the reference
	 * count for music->list should be 1. Simply reduce it
	 * to 0 to remove the memory used.
	 */
	g_object_unref(music->list);
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

void recurse_dir(music_player_t *music, char *p)
{
	DIR *dirp;
	struct dirent *entry;
	struct stat file_buf;
	GtkTreeIter iter;
	gchar pbuf[4000];
	gchar *duration, *path;

	dirp = opendir(p);
	while ( (entry = readdir(dirp))) {
		if (!strncmp(entry->d_name, "..", 2) || (!strncmp(entry->d_name, ".", 1)))
			continue;
		snprintf(pbuf, 3999, "%s/%s", p, entry->d_name);
		stat(pbuf, &file_buf);
		if (file_buf.st_mode & S_IFDIR)
			recurse_dir(music, pbuf);
		else if (file_buf.st_mode & S_IFREG) {
			/*
			 * g_slist_insert will not make a copy of the
			 * data passed, so we need to allocate some memory
			 * for the song path on the heap before calling it.
			 */
			path = g_malloc(strlen(pbuf) + 1);
			strncpy(path, pbuf, strlen(pbuf));
			path[strlen(pbuf)] = '\0';
			music->paths = g_slist_insert(music->paths, (gpointer)path,
										  music->song_cnt);
			gtk_list_store_append(music->list, &iter);
			gtk_list_store_set(music->list, &iter, 0, ++music->song_cnt,
							   1, song_name(pbuf), -1);
		}
	}
	closedir(dirp);
}

void recurse_directories(music_player_t *music, GSList *list)
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
		recurse_dir(music, (char *)ptr->data);
		g_free(ptr->data);
		ptr = ptr->next;
	}
}

void set_volume(GtkRange *range, gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;

	AUDIO_VOL(music) = gtk_range_get_value(range);
	audio_set_volume(music);
}

void play_song(GtkTreeView *tree_view, GtkTreePath *p,
			   GtkTreeViewColumn *column,
			   gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	guint num;

	uncolorize_last_song(music);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(music->list),
							&music->iter, p);
	num = tree_view_row(GTK_TREE_MODEL(music->list), &music->iter);
	decode_audio_file(music, (gchar *)g_slist_nth_data(music->paths, num));
	colorize_song_playing(music);
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
	audio_stop_song((music_player_t *)user_data);
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

gboolean tree_view_menu_popup(GtkWidget *widget, GdkEvent *event,
			  				  gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;

	if (((GdkEventButton *)event)->button != 3)
		return FALSE;

	return FALSE;
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

void open_file(GtkMenuItem *menu_item, gpointer user_data)
{
	GtkWidget *dialog;
	GtkTreeIter iter;
	GtkFileChooser *chooser;
	music_player_t *music = (music_player_t *)user_data;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	gint retval;
	GSList *filenames, *ptr;
	gchar *duration;

	dialog = gtk_file_chooser_dialog_new("Open File", GTK_WINDOW(music->window),
										 action, "Open", GTK_RESPONSE_ACCEPT,
										 "Cancel", GTK_RESPONSE_CANCEL, NULL);
	chooser = GTK_FILE_CHOOSER(dialog);
	gtk_file_chooser_set_select_multiple(chooser, TRUE);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));

	if (retval == GTK_RESPONSE_ACCEPT) {
		filenames = gtk_file_chooser_get_filenames(chooser), ptr = filenames;
		while (ptr) {
			if (!is_dir((gchar *)ptr->data)) {
				/*
				 * Save a reference to the path itself.
				 */
				music->paths = g_slist_insert(music->paths, ptr->data,
											  music->song_cnt);
				gtk_list_store_append(music->list, &iter);
				gtk_list_store_set(music->list, &iter, 0, ++music->song_cnt,
							   	   1, song_name((gchar *)ptr->data), -1);
			}
			ptr = ptr->next;
		}
		g_slist_free(filenames);
	}
	gtk_widget_destroy(dialog);
}

void open_folder(GtkMenuItem *menu_item, gpointer user_data)
{
	GtkWidget *dialog;
	GtkFileChooser *chooser;
	music_player_t *music = (music_player_t *)user_data;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
	gint retval;
	GSList *filenames;
	guint num = 0;

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
		if (music->paths)
			music->paths_len = g_slist_length(music->paths);
		else
			music->paths_len = 0;
		recurse_directories(music, filenames);
		g_slist_free(filenames);

		schedule_duration_thread(music);
	}
	gtk_widget_destroy(dialog);
}

void *duration_thread(void *user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	gchar *duration, buf[10];
	GtkTreeIter iter;
	GSList *ptr;

	snprintf(buf, 9, "%d", music->paths_len);
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(music->list),
									    &iter, buf);
	ptr = g_slist_nth(music->paths, music->paths_len);
	while (ptr) {
		duration = get_duration((gchar *)ptr->data);
		if (duration) {
			gtk_list_store_set(music->list, &iter, 2, duration, -1);
			g_free(duration);
		}
		gtk_tree_model_iter_next(GTK_TREE_MODEL(music->list), &iter);
		ptr = ptr->next;
	}
}

void schedule_duration_thread(music_player_t *music)
{
	pthread_create(&music->dur_tid, NULL, duration_thread,
				   music);
}

void tree_view_delete(GtkWidget *widget, GdkEvent *event,
					  gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
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
	list = gtk_tree_selection_get_selected_rows(selection, (GtkTreeModel **)&music->list);
	rows = (GtkTreeRowReference **)g_malloc(sizeof(GtkTreeRowReference *)
										    * num_rows);
	ptr = list;
	while (ptr) {
		rows[i++] = gtk_tree_row_reference_new(GTK_TREE_MODEL(music->list),
				   							   (GtkTreePath *)ptr->data);
		ptr = ptr->next;
	}
	g_list_free_full(list, (GDestroyNotify)gtk_tree_path_free);
	for (i = 0; i < num_rows; i++) {
		p = gtk_tree_row_reference_get_path(rows[i]);
		gtk_tree_model_get_iter(GTK_TREE_MODEL(music->list), &iter, p);
		gtk_tree_row_reference_free(rows[i]);
		gtk_tree_path_free(p);
		

		entry = g_slist_nth(music->paths, tree_view_row(GTK_TREE_MODEL(music->list),
													    &iter));
		g_free(entry->data);
		music->paths = g_slist_delete_link(music->paths, entry);
		gtk_list_store_remove(music->list, &iter);
	}
	g_free(rows);
}

void start_progress_scale(music_player_t *music, guint max)
{
	/*
	 * max points to the duration of the song.
	 */
	gtk_range_set_range(GTK_RANGE(music->progress_scale), 0, max);

	pthread_create(&music->tid, NULL, incr_progress_scale,
				   music);
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
	pthread_cancel(music->tid);
	music->seconds = 0;
}

void *incr_progress_scale(void *user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	GtkAdjustment *adjustment;
	guint max;

	adjustment = gtk_range_get_adjustment(GTK_RANGE(
										  music->progress_scale));
	max = gtk_adjustment_get_upper(adjustment);

	while (1) {
		if (music->pause) {
			pthread_mutex_lock(&music->mutex);
			pthread_cond_wait(&music->cond, &music->mutex);
			pthread_mutex_unlock(&music->mutex);
		}
		pthread_testcancel();
		if (music->seconds >= max)
			break;
		gtk_range_set_value(GTK_RANGE(music->progress_scale),
							music->seconds);
		music->seconds++;
		sleep(1);
	}
	music->seconds = 0;
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
	usleep(1000 * 20);

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

void next_song(music_player_t *music)
{
	gchar *filename = NULL;
	guint num, retval;

	if (music->repeat) {
		num = tree_view_row(GTK_TREE_MODEL(music->list), &music->iter);
		filename = (gchar *)g_slist_nth_data(music->paths, num);
		decode_audio_file(music, filename);
		return;
	}
	uncolorize_last_song(music);
	do { 
		retval = gtk_tree_model_iter_next(GTK_TREE_MODEL(music->list),
									  	  &music->iter);
		if (!retval) {
			gtk_range_set_range(GTK_RANGE(music->progress_scale), 0, 0);
			return;
		}
		num = tree_view_row(GTK_TREE_MODEL(music->list), &music->iter);
		filename = (gchar *)g_slist_nth_data(music->paths, num);
	} while (!decode_audio_file(music, filename));
	colorize_song_playing(music);
}

/*
 * Gui area;
 */
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
	setup_tree_view(music);
	add_playlist(music);

	gtk_widget_show_all(music->window);
}

GtkWidget *init_gtk(int *argc, char **argv)
{
	GtkWidget *window;

	gtk_init(argc, &argv);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!window)
		return NULL;
	gtk_window_set_title(GTK_WINDOW(window), "Lightweight Audio Player");
	gtk_window_set_default_size(GTK_WINDOW(window), 600, 600);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	return window;
}

void setup_boxes(music_player_t *music)
{
	music->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	gtk_container_add(GTK_CONTAINER(music->window), music->vbox);
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
	gtk_container_add(GTK_CONTAINER(music->menu_item_open_file), box1);

	box2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	icon2 = gtk_image_new_from_icon_name("folder-symbolic", GTK_ICON_SIZE_MENU);
	gtk_container_add(GTK_CONTAINER(box2), icon2);
	label2 = gtk_label_new("Open Folder");
	gtk_container_add(GTK_CONTAINER(box2), label2);
	music->menu_item_open_folder = gtk_menu_item_new();
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

	music->tool_item_volume = gtk_tool_item_new();
	music->volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
												   0, PA_VOLUME_NORM, 1.0);
	gtk_scale_set_draw_value(GTK_SCALE(music->volume_scale), FALSE);
	gtk_range_set_value(GTK_RANGE(music->volume_scale), PA_VOLUME_NORM);
	gtk_widget_set_size_request(music->volume_scale, 150, 10);
	gtk_container_add(GTK_CONTAINER(music->tool_item_volume), music->volume_scale);
	gtk_toolbar_insert(GTK_TOOLBAR(music->toolbar), music->tool_item_volume, -1);
	g_signal_connect(G_OBJECT(music->volume_scale), "value-changed",
					 G_CALLBACK(set_volume), music);
	/*
	 * Lastly pack the toolbar into the vbox.
	 */
	gtk_box_pack_start(GTK_BOX(music->vbox), music->toolbar, FALSE, FALSE, 0);
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

void setup_tree_view_menu_popup(music_player_t *music)
{
	music->tree_view_menu = gtk_menu_new();

	music->menu_item_play = gtk_menu_item_new_with_label("Play");
	gtk_menu_shell_append(GTK_MENU_SHELL(music->tree_view_menu),
						  music->menu_item_play);

	music->menu_item_pause = gtk_menu_item_new_with_label("Pause");
	gtk_menu_shell_append(GTK_MENU_SHELL(music->tree_view_menu),
						  music->menu_item_pause);

	music->menu_item_stop = gtk_menu_item_new_with_label("Stop");
	gtk_menu_shell_append(GTK_MENU_SHELL(music->tree_view_menu),
						  music->menu_item_stop);

	music->menu_item_next = gtk_menu_item_new_with_label("Next");
	gtk_menu_shell_append(GTK_MENU_SHELL(music->tree_view_menu),
						  music->menu_item_next);

	music->menu_item_prev = gtk_menu_item_new_with_label("Prev");
	gtk_menu_shell_append(GTK_MENU_SHELL(music->tree_view_menu),
						  music->menu_item_prev);

	music->menu_item_info = gtk_menu_item_new_with_label("Information");
	gtk_menu_shell_append(GTK_MENU_SHELL(music->tree_view_menu),
						  music->menu_item_info);
	gtk_widget_show_all(music->tree_view_menu);
}

void setup_tree_view(music_player_t *music)
{
	GtkTreeSelection *selection;

	music->scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(music->scrolled_window),
								   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	music->tree_view = gtk_tree_view_new();
	
	g_signal_connect(G_OBJECT(music->tree_view), "row-activated",
					 G_CALLBACK(play_song), music);
	g_signal_connect(G_OBJECT(music->tree_view), "button-press-event",
					 G_CALLBACK(tree_view_menu_popup), music);
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

	gtk_box_pack_start(GTK_BOX(music->vbox), music->scrolled_window, TRUE, TRUE, 0);
	
	music->list = gtk_list_store_new(5, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING,
									 G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(music->tree_view),
							GTK_TREE_MODEL(music->list));
	setup_tree_view_menu_popup(music);
}

void uncolorize_last_song(music_player_t *music)
{
	if (!gtk_list_store_iter_is_valid(music->list, &music->iter))
		return;
	gtk_list_store_set(music->list, &music->iter, 3,
					   "white", -1);
}

void colorize_song_playing(music_player_t *music)
{
	gtk_list_store_set(music->list, &music->iter, 3, "gray",
					   -1);
}

void add_playlist(music_player_t *music)
{
	GtkTreeIter iter;
	struct passwd *p;
	int fd, len;
	char *buf;
	char *ptr, *ptr2;
	struct stat file_buf;
	gchar *path;

	p = getpwuid(getuid());
	if (!p)
		return;
	buf = (char *)malloc(strlen(p->pw_dir) + 8);
	snprintf(buf, strlen(p->pw_dir) + 8, "%s/.audio", p->pw_dir);

	fd = open(buf, O_RDONLY, 0);
	free(buf);
	
	if (fd == -1)
		goto err;
	if (fstat(fd, &file_buf) == -1)
		goto err;

	buf = (char *)malloc(sizeof(char) * file_buf.st_size);
	len = read(fd, buf, file_buf.st_size);
	for (ptr = buf, ptr2 = buf; *ptr != '\0'; ptr++) {
		if (!strncmp(ptr, "<<__0x40414243__>>", 18)) {
			*ptr = '\0';
			path = g_malloc(strlen(ptr2) + 1);
			strncpy(path, ptr2, strlen(ptr2) + 1);
			music->paths = g_slist_insert(music->paths, path,
										  music->song_cnt);
			gtk_list_store_append(music->list, &iter);
			gtk_list_store_set(music->list, &iter, 0, ++music->song_cnt,
							   1, song_name(ptr2), -1);
			ptr2 = ptr += 18;
			if (*ptr != '\n' && *ptr != '\0') {
				for (; *ptr != '\n'; ptr++);
				*ptr = '\0';
				gtk_list_store_set(music->list, &iter, 2, ptr2, -1);
			}
			ptr2 = ptr + 1;
		}
	}
	free(buf);
err:
	close(fd);
}

void save_playlist(music_player_t *music)
{
	struct passwd *p;
	int fd, i = 0;
	char *buf, *duration;
	char obuf[4000];
	GtkTreeIter iter;
	GSList *ptr;
	gchar *path; 

	p = getpwuid(getuid());
	if (!p)
		return;
	buf = (char *)malloc(strlen(p->pw_dir) + 8);
	snprintf(buf, strlen(p->pw_dir) + 8, "%s/.audio", p->pw_dir);
	fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR);
	free(buf);

	/*
	 * Now we can write the playlist to .audio.
	 */
	gtk_tree_model_get_iter_first(GTK_TREE_MODEL(music->list), &iter);
	ptr = music->paths;
	while (ptr) {
		path = (gchar *)g_slist_nth_data(music->paths, i++);
		snprintf(obuf, 3994, "%s<<__0x40414243__>>", path);
	
		gtk_tree_model_get(GTK_TREE_MODEL(music->list), &iter,
						   2, &duration, -1);
		if (duration) {
			strncat(obuf, duration, strlen(duration));
			g_free(duration);
		}
		strncat(obuf, "\n", 1);
		write(fd, obuf, strlen(obuf));
		g_free(path);
		ptr = ptr->next;

		gtk_tree_model_iter_next(GTK_TREE_MODEL(music->list), &iter);
	}
	close(fd);
}