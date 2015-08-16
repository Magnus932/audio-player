
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <err.h>
#include <pwd.h>
#include "include/generic.h"

GtkWidget *init_gtk(int *argc, char **argv)
{
	GtkWidget *window;

	gtk_init(argc, &argv);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!window)
		return NULL;
	gtk_window_set_title(GTK_WINDOW(window), "Lightweight Audio Player");
	gtk_window_set_default_size(GTK_WINDOW(window), 750, 500);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	return window;
}

void add_playlist(music_player_t *music)
{
	GtkTreeIter iter;
	struct passwd *p;
	int fd, len;
	char *buf;
	char *ptr, *ptr2;
	struct stat file_buf;

	p = getpwuid(getuid());
	if (!p) {
		fprintf(stderr, "Error; %s\n", strerror(errno));
		return;
	}
	buf = (char *)malloc(strlen(p->pw_dir) + 8);
	snprintf(buf, strlen(p->pw_dir) + 8, "%s/.audio", p->pw_dir);

	fd = open(buf, O_RDONLY, 0);
	free(buf);
	if (fd == -1)
		return;
	if (fstat(fd, &file_buf) == -1)
		return;
	buf = (char *)malloc(sizeof(char) * file_buf.st_size);
	len = read(fd, buf, file_buf.st_size);
	buf[len] = '\0';
	for (ptr = buf, ptr2 = buf; *ptr != '\0'; ptr++) {
		if (*ptr == ',') {
			*ptr = '\0';
			gtk_list_store_append(music->list, &iter);
			gtk_list_store_set(music->list, &iter, 0, ++music->song_cnt,
							   1, ptr2, -1);
			ptr2 = ++ptr;
			if (*ptr != '\n' && *ptr != '\0') {
				for (; *ptr != '\n'; ptr++);
				*ptr = '\0';
				gtk_list_store_set(music->list, &iter, 2, ptr2, -1);
			}
			ptr2 = ptr + 1;
		}
	}
	free(buf);
	close(fd);
}

void save_playlist(music_player_t *music)
{
	struct passwd *p;
	int fd, retval;
	char *buf, *filename;
	char *duration;
	GtkTreeIter iter;
	char obuf[1024];

	p = getpwuid(getuid());
	if (!p) {
		fprintf(stderr, "Error; %s\n", strerror(errno));
		return;
	}
	buf = (char *)malloc(strlen(p->pw_dir) + 8);
	snprintf(buf, strlen(p->pw_dir) + 8, "%s/.audio", p->pw_dir);
	fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR);
	free(buf);

	/*
	 * Now we can write the playlist to .audio.
	 */
	retval = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(music->list),
								  		   &iter);
	if (!retval)
		return;
	do {
		gtk_tree_model_get(GTK_TREE_MODEL(music->list), &iter,
						   1, &filename, 2, &duration, -1);
		snprintf(obuf, 1023, "%s,", filename);
		if (duration) {
			strncat(obuf, duration, strlen(duration));
			g_free(duration);
		}
		strncat(obuf, "\n", 1);
		write(fd, obuf, strlen(obuf));
		g_free(filename);
	} while (gtk_tree_model_iter_next(GTK_TREE_MODEL(music->list),
			 &iter));
	close(fd);
}

void exit_nicely(music_player_t *music)
{
	audio_stop_song(music);
	audio_close_connection(music);
	save_playlist(music);
	gtk_widget_destroy(music->window);
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
	gchar pbuf[1024];
	gchar *duration;

	dirp = opendir(p);
	while ( (entry = readdir(dirp))) {
		if (!strncmp(entry->d_name, "..", 2) || (!strncmp(entry->d_name, ".", 1)))
			continue;
		snprintf(pbuf, 1023, "%s/%s", p, entry->d_name);
		stat(pbuf, &file_buf);
		if (file_buf.st_mode & S_IFDIR)
			recurse_dir(music, pbuf);
		else if (file_buf.st_mode & S_IFREG) {
			gtk_list_store_append(music->list, &iter);
			gtk_list_store_set(music->list, &iter, 0, ++music->song_cnt,
							   1, pbuf, -1);
			duration = get_duration(pbuf);
			if (duration) {
				gtk_list_store_set(music->list, &iter, 2, duration, -1);
				g_free(duration);
			}
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

void play_song(GtkToolButton *tool_button,
			   gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	GtkTreeSelection *s;
	GtkTreeModel **model;
	GList *list;
	gchar *filename;

	if (AUDIO_STREAM(music)) {
		audio_unpause_song(music);
	}
	else {
		s = gtk_tree_view_get_selection(GTK_TREE_VIEW(
										music->tree_view));
		model = (GtkTreeModel **)&music->list;
		list = gtk_tree_selection_get_selected_rows(s, model);
		
		if (!list)
			return;
		if (gtk_list_store_iter_is_valid(music->list, &music->iter))
			gtk_list_store_set(music->list, &music->iter, 3,
							   "white", 4, "black", -1);
		gtk_tree_model_get_iter(*model, &music->iter,
								(GtkTreePath *)list->data);
		gtk_list_store_set(music->list, &music->iter, 3, "#222327",
					   4, "white", -1);
		gtk_tree_model_get(*model, &music->iter, 1, &filename,
						   -1);
		decode_audio_file(music, filename);

		g_list_free_full(list, (GDestroyNotify)gtk_tree_path_free);
		g_free(filename);
	}
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

void play_audio_callback(GtkTreeView *tree_view, GtkTreePath *p,
						 GtkTreeViewColumn *column,
						 gpointer user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	gchar *filename;

	if (gtk_list_store_iter_is_valid(music->list, &music->iter))
		gtk_list_store_set(music->list, &music->iter, 3,
						   "white", 4, "black", -1);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(music->list),
							&music->iter, p);
	gtk_tree_model_get(GTK_TREE_MODEL(music->list), &music->iter,
					   1, &filename, -1);
	gtk_list_store_set(music->list, &music->iter, 3, "#222327",
					   4, "white", -1);
	decode_audio_file(music, filename);

	g_free(filename);
}

/*
 * Due to the fact that showing the duration of each
 * song was not in the picture, this was simply a fix
 * to deny further writing of code, by passing a dummy
 * music_player_t to init_song_context.
 */
gchar *get_duration(gchar *fname)
{
	music_player_t music;
	int duration;
	int minutes, seconds;
	gchar *buf;

	if (!init_song_context(&music, fname))
		return NULL;
	buf = g_malloc(sizeof(gchar) * 20);
	duration = get_song_duration(&music);

	minutes = duration / 60;
	seconds = duration % 60;
	snprintf(buf, 19, "%s%d:%s%d", (minutes < 10) ? "0" : "",
					   minutes, (seconds < 10) ? "0" : "", seconds);
	cleanup_decoder(&music);

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
			if (!IS_DIR((gchar *)ptr->data)) {
				gtk_list_store_append(music->list, &iter);
				gtk_list_store_set(music->list, &iter, 0, ++music->song_cnt,
							   	   1, (gchar *)ptr->data, -1);
				duration = get_duration((gchar *)ptr->data);
				if (duration) {
					gtk_list_store_set(music->list, &iter, 2, duration, -1);
					g_free(duration);
				}
			}
			g_free(ptr->data);
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

	dialog = gtk_file_chooser_dialog_new("Open Folder", GTK_WINDOW(music->window),
										 action, "Open", GTK_RESPONSE_ACCEPT,
										 "Cancel", GTK_RESPONSE_CANCEL, NULL);
	chooser = GTK_FILE_CHOOSER(dialog);
	gtk_file_chooser_set_select_multiple(chooser, TRUE);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));
	if (retval == GTK_RESPONSE_ACCEPT) {
		filenames = gtk_file_chooser_get_filenames(chooser);
		recurse_directories(music, filenames);
		g_slist_free(filenames);
	}
	gtk_widget_destroy(dialog);
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
	guint num_rows, i = 0;

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
		gtk_list_store_remove(music->list, &iter);
	}
	g_free(rows);
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

void setup_boxes(music_player_t *music)
{
	music->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	gtk_container_add(GTK_CONTAINER(music->window), music->vbox);
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
					 G_CALLBACK(play_song), music);

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

void stop_progress_scale(music_player_t *music)
{
	pthread_cancel(music->tid);
	music->seconds = 0;
}

/*
 * Todo;
 * - Implement condition blocking when paused/resumed.
 */
void *incr_progress_scale(void *user_data)
{
	music_player_t *music = (music_player_t *)user_data;
	GtkAdjustment *adjustment;
	guint max;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	adjustment = gtk_range_get_adjustment(GTK_RANGE(
										  music->progress_scale));
	max = gtk_adjustment_get_upper(adjustment);
	while (1) {
		if (music->seconds >= max)
			break;
		gtk_range_set_value(GTK_RANGE(music->progress_scale),
							music->seconds);
		music->seconds++;
		sleep(1);
	}
	music->seconds = 0;
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

void setup_tree_view(music_player_t *music)
{
	GtkTreeSelection *selection;

	music->scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(music->scrolled_window),
								   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	music->tree_view = gtk_tree_view_new();
	
	g_signal_connect(G_OBJECT(music->tree_view), "row-activated",
					 G_CALLBACK(play_audio_callback), music);
	g_signal_connect(G_OBJECT(music->tree_view), "key-press-event",
					 G_CALLBACK(tree_view_delete), music);
	gtk_container_add(GTK_CONTAINER(music->scrolled_window), music->tree_view);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(music->tree_view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

	music->num_song = gtk_tree_view_column_new();
	music->num_rend = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(music->num_song, music->num_rend, FALSE);
	gtk_tree_view_column_set_attributes(music->num_song, music->num_rend,
										"text", 0, "cell-background", 3,
										"foreground", 4, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(music->tree_view), music->num_song);

	music->song = gtk_tree_view_column_new();
	music->song_rend = gtk_cell_renderer_text_new();
	gtk_tree_view_column_set_title(music->song, "Song");
	gtk_tree_view_column_pack_start(music->song, music->song_rend, FALSE);
	gtk_tree_view_column_set_attributes(music->song, music->song_rend,
										"text", 1, "cell-background", 3,
										"foreground", 4, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(music->tree_view), music->song);

	music->dur = gtk_tree_view_column_new();
	music->dur_rend = gtk_cell_renderer_text_new();
	gtk_tree_view_column_set_title(music->dur, "Duration");
	gtk_tree_view_column_pack_start(music->dur, music->dur_rend, FALSE);
	gtk_tree_view_column_set_attributes(music->dur, music->dur_rend,
										"text", 2, "cell-background", 3,
										"foreground", 4, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(music->tree_view), music->dur);

	gtk_box_pack_start(GTK_BOX(music->vbox), music->scrolled_window, TRUE, TRUE, 0);
	
	music->list = gtk_list_store_new(5, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING,
									 G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(music->tree_view),
							GTK_TREE_MODEL(music->list));
}

void next_song(music_player_t *music)
{
	char *filename = NULL;
	gboolean retval;

	if (music->repeat) {
		gtk_tree_model_get(GTK_TREE_MODEL(music->list), &music->iter,
						   1, &filename, -1);
		decode_audio_file(music, filename);
		g_free(filename);
		return;
	}
	gtk_list_store_set(music->list, &music->iter, 3, "white", 4, "black", -1);
	do {
		if (filename) {
			g_free(filename);
			filename = NULL;
		} 
		retval = gtk_tree_model_iter_next(GTK_TREE_MODEL(music->list),
									  	  &music->iter);
		if (!retval) {
			gtk_range_set_range(GTK_RANGE(music->progress_scale), 0, 0);
			return;
		}
		gtk_tree_model_get(GTK_TREE_MODEL(music->list), &music->iter,
						   1, &filename, -1);
	} while (!decode_audio_file(music, filename));
	gtk_list_store_set(music->list, &music->iter, 3, "#222327",
					   4, "white", -1);

	g_free(filename);
}

int main(int argc, char **argv)
{
	music_player_t *music;

	music = (music_player_t *)malloc(sizeof(*music));	
	memset(music, '\0', sizeof(*music));

	music->window = init_gtk(&argc, argv);
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

	init_decoder(music);
	init_audio(music);
	gtk_widget_show_all(music->window);

	gtk_main();
}