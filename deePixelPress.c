#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_PATH 4096
#define COMMAND_SIZE 524288
#define MAX_THREADS 4
#define CONFIG_FILE "deePixelPress.conf"

typedef struct {
    char *filename;
    char *dimension;
    char *resolution;
    char *savings;
    gboolean selected;
} FileInfo;

typedef struct {
    char *input_file;
    char *output_file;
    int compression_level;
} CompressionJob;

GtkWidget *window;
GtkWidget *file_list_view;
GtkListStore *file_list_store;
GtkWidget *original_image;
GtkWidget *compressed_image;
GtkWidget *compression_scale;
GtkWidget *compression_label;
GtkWidget *status_label;
GtkWidget *compress_button;
GtkWidget *save_button;
GtkWidget *metadata_view;


int total_files = 0;
int processed_files = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

char *current_filename = NULL;
char *current_compressed_filename = NULL;

gboolean webp_supported = FALSE;
gboolean heif_supported = FALSE;
gboolean avif_supported = FALSE;

GQueue *compression_queue;
GThread *compression_thread = NULL;

// Function prototypes
void create_gui(void);
void add_file_to_list(const char *filename);
void on_add_clicked(GtkWidget *widget, gpointer data);
void on_remove_clicked(GtkWidget *widget, gpointer data);
void on_compress_clicked(GtkWidget *widget, gpointer data);
void on_save_clicked(GtkWidget *widget, gpointer data);
gboolean on_decrease_compression(GtkWidget *widget, gpointer data);
gboolean on_increase_compression(GtkWidget *widget, gpointer data);
void update_compression_label(void);
void update_compression_label_text(void);
bool compress_image(const char *input_file, const char *output_file, int compression_level);
void *compression_thread_func(gpointer data);
gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
void load_image(GtkWidget *image, const char *filename);
void on_window_resize(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data);
void on_file_selected(GtkTreeSelection *selection, gpointer data);
void on_remove_metadata_clicked(GtkWidget *widget, gpointer data);
void update_metadata_display(const char *filename);
void apply_css(void);
void cleanup(void);
void on_window_destroy(GtkWidget *widget, gpointer data);
void save_config(void);
void load_config(void);
void on_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                           GtkSelectionData *data, guint info, guint time, gpointer user_data);

void create_gui(void) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "deePixelPress");
    gtk_window_set_default_size(GTK_WINDOW(window), 1400, 900);
    
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(main_box), left_box, FALSE, FALSE, 0);

    GtkWidget *file_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(left_box), file_list_box, TRUE, TRUE, 0);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolled_window, 400, 400);
    gtk_box_pack_start(GTK_BOX(file_list_box), scrolled_window, TRUE, TRUE, 0);

    file_list_store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
    file_list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(file_list_store));
    gtk_container_add(GTK_CONTAINER(scrolled_window), file_list_view);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list_view), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Dimension", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list_view), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Resolution", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list_view), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Savings", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list_view), column);

    renderer = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer, "active", 4, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list_view), column);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(file_list_view));
    g_signal_connect(selection, "changed", G_CALLBACK(on_file_selected), NULL);

    GtkWidget *metadata_frame = gtk_frame_new("Metadata");
    gtk_box_pack_start(GTK_BOX(left_box), metadata_frame, FALSE, FALSE, 0);

    GtkWidget *metadata_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(metadata_scroll, 400, 200);
    gtk_container_add(GTK_CONTAINER(metadata_frame), metadata_scroll);

    metadata_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(metadata_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(metadata_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(metadata_scroll), metadata_view);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(left_box), button_box, FALSE, FALSE, 0);

    GtkWidget *add_button = gtk_button_new_with_label("Add");
    gtk_widget_set_name(add_button, "myButton_green");
    g_signal_connect(add_button, "clicked", G_CALLBACK(on_add_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), add_button, TRUE, TRUE, 0);

    GtkWidget *remove_button = gtk_button_new_with_label("Remove");
    gtk_widget_set_name(remove_button, "myButton_red");
    g_signal_connect(remove_button, "clicked", G_CALLBACK(on_remove_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), remove_button, TRUE, TRUE, 0);

    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(main_box), right_box, TRUE, TRUE, 0);

    GtkWidget *image_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(right_box), image_box, TRUE, TRUE, 0);

    GtkWidget *original_frame = gtk_aspect_frame_new(NULL, 0.5, 0.5, 1, FALSE);
    gtk_frame_set_shadow_type(GTK_FRAME(original_frame), GTK_SHADOW_IN);
    gtk_widget_set_size_request(original_frame, 256, 256);
    gtk_box_pack_start(GTK_BOX(image_box), original_frame, TRUE, TRUE, 0);

    original_image = gtk_image_new();
    gtk_container_add(GTK_CONTAINER(original_frame), original_image);

    GtkWidget *compressed_frame = gtk_aspect_frame_new(NULL, 0.5, 0.5, 1, FALSE);
    gtk_frame_set_shadow_type(GTK_FRAME(compressed_frame), GTK_SHADOW_IN);
    gtk_widget_set_size_request(compressed_frame, 256, 256);
    gtk_box_pack_start(GTK_BOX(image_box), compressed_frame, TRUE, TRUE, 0);

    compressed_image = gtk_image_new();
    gtk_container_add(GTK_CONTAINER(compressed_frame), compressed_image);

    GtkWidget *compression_frame = gtk_frame_new("Compression Options");
    gtk_box_pack_start(GTK_BOX(right_box), compression_frame, FALSE, FALSE, 0);

    GtkWidget *compression_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(compression_frame), compression_box);

    GtkWidget *decrease_button = gtk_button_new_with_label(" < ");
    gtk_widget_set_size_request(decrease_button, 40, -1);  // Set width to 40 pixels
    gtk_box_pack_start(GTK_BOX(compression_box), decrease_button, FALSE, FALSE, 0);
    g_signal_connect(decrease_button, "clicked", G_CALLBACK(on_decrease_compression), NULL);

    compression_label = gtk_label_new("Compression Level: Medium JPG");
    gtk_box_pack_start(GTK_BOX(compression_box), compression_label, TRUE, TRUE, 0);

    GtkWidget *increase_button = gtk_button_new_with_label(" > ");
    gtk_widget_set_size_request(increase_button, 40, -1);  // Set width to 40 pixels
    gtk_box_pack_start(GTK_BOX(compression_box), increase_button, FALSE, FALSE, 0);
    g_signal_connect(increase_button, "clicked", G_CALLBACK(on_increase_compression), NULL);

    status_label = gtk_label_new("Ready");
    gtk_box_pack_start(GTK_BOX(right_box), status_label, FALSE, FALSE, 0);

    compress_button = gtk_button_new_with_label("Compress");
    gtk_widget_set_name(compress_button, "myButton_blue");
    g_signal_connect(compress_button, "clicked", G_CALLBACK(on_compress_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(right_box), compress_button, FALSE, FALSE, 0);

    GtkWidget *remove_metadata_button = gtk_button_new_with_label("Remove Metadata & Save");
    gtk_widget_set_name(remove_metadata_button, "myButton_purple");
    g_signal_connect(remove_metadata_button, "clicked", G_CALLBACK(on_remove_metadata_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(right_box), remove_metadata_button, FALSE, FALSE, 0);

    save_button = gtk_button_new_with_label("Save");
    gtk_widget_set_name(save_button, "myButton_blue");
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(right_box), save_button, FALSE, FALSE, 0);

    gtk_drag_dest_set(window, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(window);
    g_signal_connect(window, "drag-data-received", G_CALLBACK(on_drag_data_received), NULL);

    apply_css();

    GtkAccelGroup *accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    gtk_widget_add_accelerator(add_button, "clicked", accel_group, GDK_KEY_a, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(compress_button, "clicked", accel_group, GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(save_button, "clicked", accel_group, GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    g_signal_connect(window, "size-allocate", G_CALLBACK(on_window_resize), NULL);
    g_signal_connect(G_OBJECT(window), "key-press-event", G_CALLBACK(on_key_press), NULL);

    gtk_widget_show_all(window);
}

void apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "window, dialog { background-color: #292831; }"
        "button { background-image: none; }"
        "label { color: #fbbbad; }"
        "frame { border-color: #4a7a96; }"
        "scrolledwindow { background-color: #292831; }"
        "treeview { background-color: #292831; color: #fbbbad; }"
        "treeview:selected { background-color: #4a7a96; }"
        "treeview header { background-color: #292831; color: #fbbbad; }"
        "scale { background-color: #333f58; color: #fbbbad; }"
        "scale trough { background-color: #4a7a96; }"
        "scale slider { background-color: #ee8695; }"
        "textview { background-color: #333f58; color: #fbbbad; }"
        "textview text { background-color: #333f58; color: #fbbbad; }"
        "#myButton_red { background-color: #ee8695; color: #292831; font-weight: bold; border-radius: 15px; }"
        "#myButton_blue { background-color: #333f58; color: #fbbbad; font-weight: bold; border-radius: 15px; }"
        "#myButton_green { background-color: #4a7a96; color: #fbbbad; font-weight: bold; border-radius: 15px; }"
        "#myButton_purple { background-color: #8d697a; color: #fbbbad; font-weight: bold; border-radius: 15px; }"
        "#myButton_red:hover, #myButton_blue:hover, #myButton_green:hover, #myButton_purple:hover { background-color: #292831; color: #fbbbad; }",
        -1, NULL);
    
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
}

void add_file_to_list(const char *filename) {
    if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
        g_print("File does not exist: %s\n", filename);
        return;
    }

    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        g_print("Not a regular file: %s\n", filename);
        return;
    }

    GtkTreeIter iter;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, NULL);
    
    if (pixbuf) {
        int width = gdk_pixbuf_get_width(pixbuf);
        int height = gdk_pixbuf_get_height(pixbuf);
        char *dimension = g_strdup_printf("%dx%d", width, height);
        char *resolution = g_strdup_printf("%d", width * height);
        
        char *basename = g_path_get_basename(filename);
        
        gtk_list_store_append(file_list_store, &iter);
        gtk_list_store_set(file_list_store, &iter,
                           0, basename,
                           1, dimension,
                           2, resolution,
                           3, "0%",
                           4, TRUE,
                           5, filename,
                           -1);
        
        g_free(dimension);
        g_free(resolution);
        g_free(basename);
        g_object_unref(pixbuf);
    }
}

int current_compression_level = 2;

gboolean on_increase_compression(GtkWidget *widget, gpointer data) {
    if (current_compression_level < 6) {
        current_compression_level++;
        update_compression_label_text();
    }
    return TRUE;
}

gboolean on_decrease_compression(GtkWidget *widget, gpointer data) {
    if (current_compression_level > 0) {
        current_compression_level--;
        update_compression_label_text();
    }
    return TRUE;
}

void update_compression_label(void) {
    const char *level_str;
    switch (current_compression_level) {
        case 0: level_str = "JPEG Low"; break;
        case 1: level_str = "JPEG Medium"; break;
        case 2: level_str = "JPEG High"; break;
        case 3: level_str = "JPEG Ultra"; break;
        case 4: level_str = "PNG Low"; break;
        case 5: level_str = "PNG Medium"; break;
        case 6: level_str = "PNG High"; break;
        default: level_str = "JPEG Low"; break;
    }
    char *label_text = g_strdup_printf("Compression Level: %s", level_str);
    gtk_label_set_text(GTK_LABEL(compression_label), label_text);
    g_free(label_text);
}

void on_add_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Open File(s)",
                                         GTK_WINDOW(window),
                                         action,
                                         "_Cancel",
                                         GTK_RESPONSE_CANCEL,
                                         "_Open",
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        GSList *filenames, *iter;
        filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        for (iter = filenames; iter; iter = iter->next) {
            char *filename = iter->data;
            add_file_to_list(filename);
            g_free(filename);
        }
        g_slist_free(filenames);
    }

    gtk_widget_destroy(dialog);
}

void on_remove_clicked(GtkWidget *widget, gpointer data) {
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(file_list_view));
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
    }
}

void update_compression_label_text(void) {
    const char *level_str;
    switch (current_compression_level) {
        case 0: level_str = "JPEG Low"; break;
        case 1: level_str = "JPEG Medium"; break;
        case 2: level_str = "JPEG High"; break;
        case 3: level_str = "JPEG Ultra"; break;
        case 4: level_str = "PNG Low"; break;
        case 5: level_str = "PNG Medium"; break;
        case 6: level_str = "PNG High"; break;
        default: level_str = "JPEG Low"; break;
    }
    char *label_text = g_strdup_printf("Compression Level: %s", level_str);
    gtk_label_set_text(GTK_LABEL(compression_label), label_text);
    g_free(label_text);
}

bool compress_image(const char *input_file, const char *output_file, int compression_level) {
    char *command = NULL;
    bool success = false;

    switch (compression_level) {
    case 0: // JPEG Low
        command = g_strdup_printf("ffmpeg -i \"%s\" -qscale:v 10 -pix_fmt yuvj420p \"%s\" -y", 
                                  input_file, output_file);
        break;
    case 1: // JPEG Medium
        command = g_strdup_printf("ffmpeg -i \"%s\" -qscale:v 5 -pix_fmt yuvj420p \"%s\" -y", 
                                  input_file, output_file);
        break;
    case 2: // JPEG High
        command = g_strdup_printf("ffmpeg -i \"%s\" -qscale:v 2 -pix_fmt yuvj420p \"%s\" -y", 
                                  input_file, output_file);
        break;
    case 3: // JPEG Ultra
        command = g_strdup_printf("ffmpeg -i \"%s\" -qscale:v 1 -qmin 1 -pix_fmt yuvj420p \"%s\" -y", 
                                  input_file, output_file);
        break;
    case 4: // PNG Low
        command = g_strdup_printf("ffmpeg -i \"%s\" -c:v png -compression_level 1 -pix_fmt rgba \"%s\" -y", 
                                  input_file, output_file);
        break;
    case 5: // PNG Medium
        command = g_strdup_printf("ffmpeg -i \"%s\" -c:v png -compression_level 5 -pix_fmt rgba \"%s\" -y", 
                                  input_file, output_file);
        break;
    case 6: // PNG High
        command = g_strdup_printf("ffmpeg -i \"%s\" -c:v png -compression_level 9 -pix_fmt rgba \"%s\" -y", 
                                  input_file, output_file);
        break;
    default:
        command = g_strdup_printf("ffmpeg -i \"%s\" -qscale:v 5 -pix_fmt yuvj420p \"%s\" -y", 
                                  input_file, output_file);
        break;
    }

    if (command) {
        int result = system(command);
        if (result != 0) {
            g_print("Error compressing image: %s (Exit code: %d)\n", input_file, result);
            g_print("Command: %s\n", command);
        } else {
            if (g_file_test(output_file, G_FILE_TEST_EXISTS)) {
                success = true;
            } else {
                g_print("Error: Compressed file not found: %s\n", output_file);
            }
        }
    }

    g_free(command);
    return success;
}


void *compression_thread_func(gpointer data) {
    while (TRUE) {
        CompressionJob *job = g_queue_pop_head(compression_queue);
        if (job == NULL) {
            break;
        }

        bool success = compress_image(job->input_file, job->output_file, job->compression_level);
        if (success) {
            gdk_threads_add_idle((GSourceFunc)load_image, g_strdup(job->output_file));
        } else {
            gdk_threads_add_idle((GSourceFunc)gtk_label_set_text, g_strdup("Error: Compression failed"));
        }

        g_free(job->input_file);
        g_free(job->output_file);
        g_free(job);

        processed_files++;
        gdk_threads_add_idle((GSourceFunc)gtk_label_set_text, g_strdup_printf("Compressed %d of %d files", processed_files, total_files));
    }

    return NULL;
}

void on_compress_clicked(GtkWidget *widget, gpointer data) {
    if (current_filename == NULL) {
        gtk_label_set_text(GTK_LABEL(status_label), "No image selected for compression");
        return;
    }

    const char *ext;
    
    if (current_compression_level <= 3) {
        ext = "jpg";
    } else {
        ext = "png";
    }
    
    char *basename = g_path_get_basename(current_filename);
    char *dirname = g_path_get_dirname(current_filename);
    
    char *dot = strrchr(basename, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    
    char *output_file = g_strdup_printf("%s/compressed_%s.%s", dirname, basename, ext);
    g_free(basename);
    g_free(dirname);

    if (!compress_image(current_filename, output_file, current_compression_level)) {
        gtk_label_set_text(GTK_LABEL(status_label), "Compression failed");
        g_free(output_file);
        return;
    }

    g_free(current_compressed_filename);
    current_compressed_filename = output_file;
    load_image(compressed_image, current_compressed_filename);

    struct stat original_stat, compressed_stat;
    if (stat(current_filename, &original_stat) == 0 && stat(current_compressed_filename, &compressed_stat) == 0) {
        double savings = 100.0 * ((double)original_stat.st_size - compressed_stat.st_size) / original_stat.st_size;
        char *savings_text;
        if (savings >= 0) {
            savings_text = g_strdup_printf("%.1f%%", savings);
        } else {
            savings_text = g_strdup_printf("+%.1f%%", -savings);
        }
        
        GtkTreeModel *model;
        GtkTreeIter iter;
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(file_list_view));
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            gtk_list_store_set(GTK_LIST_STORE(model), &iter, 3, savings_text, -1);
        }
        g_free(savings_text);
    }

    gtk_label_set_text(GTK_LABEL(status_label), "Compression completed");
    
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

void load_image(GtkWidget *image, const char *filename) {
    if (filename == NULL) {
        g_print("Error: Filename is NULL\n");
        gtk_image_clear(GTK_IMAGE(image));
        return;
    }

    if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
        g_print("Error: File does not exist: %s\n", filename);
        gtk_image_clear(GTK_IMAGE(image));
        return;
    }

    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    if (pixbuf) {
        GtkWidget *frame = gtk_widget_get_parent(image);
        GtkAllocation allocation;
        gtk_widget_get_allocation(frame, &allocation);

        int frame_size = allocation.width < allocation.height ? allocation.width : allocation.height;
        
        int orig_width = gdk_pixbuf_get_width(pixbuf);
        int orig_height = gdk_pixbuf_get_height(pixbuf);
        
        double ratio = (double)orig_width / orig_height;
        int new_width, new_height;
        
        if (ratio > 1) {
            new_width = frame_size;
            new_height = (int)(frame_size / ratio);
        } else {
            new_height = frame_size;
            new_width = (int)(frame_size * ratio);
        }
        
        GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple(pixbuf, new_width, new_height, GDK_INTERP_BILINEAR);
        
        gtk_image_clear(GTK_IMAGE(image));
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), scaled_pixbuf);
        g_object_unref(scaled_pixbuf);
        g_object_unref(pixbuf);
    } else {
        g_print("Error loading image %s: %s\n", filename, error->message);
        g_error_free(error);
        gtk_image_clear(GTK_IMAGE(image));
    }
    
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

void on_window_resize(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data) {
    static int last_width = 0;
    static int last_height = 0;

    if (allocation->width != last_width || allocation->height != last_height) {
        last_width = allocation->width;
        last_height = allocation->height;

        int new_size = (int)(allocation->width * 0.4);
        new_size = (new_size < 256) ? 256 : new_size;

        GtkWidget *original_frame = gtk_widget_get_parent(original_image);
        GtkWidget *compressed_frame = gtk_widget_get_parent(compressed_image);

        gtk_widget_set_size_request(original_frame, new_size, new_size);
        gtk_widget_set_size_request(compressed_frame, new_size, new_size);

        if (current_filename) {
            load_image(original_image, current_filename);
        }
        if (current_compressed_filename) {
            load_image(compressed_image, current_compressed_filename);
        }
    }
}

void on_file_selected(GtkTreeSelection *selection, gpointer data) {
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        g_free(current_filename);
        gtk_tree_model_get(model, &iter, 5, &current_filename, -1);
        
        load_image(original_image, current_filename);
        update_metadata_display(current_filename);

        gtk_image_clear(GTK_IMAGE(compressed_image));
        g_free(current_compressed_filename);
        current_compressed_filename = NULL;
    }
}

void update_metadata_display(const char *filename) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(metadata_view));
    gtk_text_buffer_set_text(buffer, "", -1);

    char command[COMMAND_SIZE];
    snprintf(command, COMMAND_SIZE, "exiftool \"%s\"", filename);

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        g_print("Failed to run command\n");
        return;
    }

    char line[1000];
    GtkTextIter end;
    while (fgets(line, sizeof(line), fp) != NULL) {
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, line, -1);
    }

    pclose(fp);
}

void on_remove_metadata_clicked(GtkWidget *widget, gpointer data) {
    if (current_compressed_filename == NULL) {
        gtk_label_set_text(GTK_LABEL(status_label), "No compressed image to remove metadata from");
        return;
    }

    GtkWidget *dialog;
    GtkFileChooser *chooser;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Save File without Metadata",
                                         GTK_WINDOW(window),
                                         action,
                                         "_Cancel",
                                         GTK_RESPONSE_CANCEL,
                                         "_Save",
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);
    chooser = GTK_FILE_CHOOSER(dialog);

    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    
    char *basename = g_path_get_basename(current_compressed_filename);
    char *suggested_name = g_strdup_printf("no_metadata_%s", basename);
    gtk_file_chooser_set_current_name(chooser, suggested_name);
    g_free(suggested_name);
    g_free(basename);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(chooser);
        
        char command[COMMAND_SIZE];
        snprintf(command, COMMAND_SIZE, "exiftool -all= -overwrite_original \"%s\" -o \"%s\"", current_compressed_filename, filename);

        int result = system(command);
        if (result == 0) {
            gtk_label_set_text(GTK_LABEL(status_label), "Metadata removed and saved successfully");
            
            g_free(current_compressed_filename);
            current_compressed_filename = g_strdup(filename);
            load_image(compressed_image, current_compressed_filename);
            
            update_metadata_display(current_compressed_filename);
        } else {
            gtk_label_set_text(GTK_LABEL(status_label), "Error removing metadata");
        }
        
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    switch(event->keyval) {
        case GDK_KEY_Left:
            on_decrease_compression(NULL, NULL);
            return TRUE;
        case GDK_KEY_Right:
            on_increase_compression(NULL, NULL);
            return TRUE;
    }
    return FALSE;
}

void on_save_clicked(GtkWidget *widget, gpointer data) {
    if (current_compressed_filename == NULL) {
        gtk_label_set_text(GTK_LABEL(status_label), "No compressed image to save");
        return;
    }

    GtkWidget *dialog;
    GtkFileChooser *chooser;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Save File",
                                         GTK_WINDOW(window),
                                         action,
                                         "_Cancel",
                                         GTK_RESPONSE_CANCEL,
                                         "_Save",
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);
    chooser = GTK_FILE_CHOOSER(dialog);

    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    
    const char *ext = strrchr(current_compressed_filename, '.');
    if (ext == NULL) ext = ".jpg";
    
    char *suggested_name = g_strdup_printf("compressed_%s", g_path_get_basename(current_filename));
    char *dot = strrchr(suggested_name, '.');
    if (dot) *dot = '\0';
    char *full_suggested_name = g_strdup_printf("%s%s", suggested_name, ext);
    
    gtk_file_chooser_set_current_name(chooser, full_suggested_name);
    g_free(suggested_name);
    g_free(full_suggested_name);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(chooser);
        
        GError *error = NULL;
        if (g_file_copy(g_file_new_for_path(current_compressed_filename),
                        g_file_new_for_path(filename),
                        G_FILE_COPY_OVERWRITE,
                        NULL, NULL, NULL, &error)) {
            gtk_label_set_text(GTK_LABEL(status_label), "File saved successfully");
        } else {
            char *error_message = g_strdup_printf("Error saving file: %s", error->message);
            gtk_label_set_text(GTK_LABEL(status_label), error_message);
            g_free(error_message);
            g_error_free(error);
        }
        
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

void cleanup() {
    g_free(current_filename);
    g_free(current_compressed_filename);
    
    while (!g_queue_is_empty(compression_queue)) {
        CompressionJob *job = g_queue_pop_head(compression_queue);
        g_free(job->input_file);
        g_free(job->output_file);
        g_free(job);
    }
    g_queue_free(compression_queue);
    
    save_config();
}


void on_window_destroy(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

void save_config() {
    GKeyFile *key_file = g_key_file_new();
    
    g_key_file_set_integer(key_file, "Settings", "CompressionLevel", current_compression_level);
    
    GError *error = NULL;
    if (!g_key_file_save_to_file(key_file, CONFIG_FILE, &error)) {
        g_warning("Error saving config file: %s", error->message);
        g_error_free(error);
    }
    
    g_key_file_free(key_file);
}

void load_config() {
    GKeyFile *key_file = g_key_file_new();
    
    if (g_key_file_load_from_file(key_file, CONFIG_FILE, G_KEY_FILE_NONE, NULL)) {
        current_compression_level = g_key_file_get_integer(key_file, "Settings", "CompressionLevel", NULL);
        update_compression_label_text();
    }
    
    g_key_file_free(key_file);
}

void on_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                           GtkSelectionData *data, guint info, guint time, gpointer user_data) {
    gchar **uris = gtk_selection_data_get_uris(data);
    
    if (uris != NULL) {
        for (int i = 0; uris[i] != NULL; i++) {
            gchar *filename = g_filename_from_uri(uris[i], NULL, NULL);
            if (filename != NULL) {
                add_file_to_list(filename);
                g_free(filename);
            }
        }
        g_strfreev(uris);
    }
    
    gtk_drag_finish(context, TRUE, FALSE, time);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    compression_queue = g_queue_new();
    
    create_gui();
    
    load_config();
    
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(on_window_destroy), NULL);
    
    gtk_main();
    cleanup();
    return 0;
}