#include <poppler.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>

/*
 *
 *  Whenever a function returns a pointer, the user has responsibility to free it.
 *  All pointers in function arguments are pre-allocated. Never use double pointer.
 *
 */

// Initialize document, return NULL on failure
void* init_pdf_document(const char *filename) {
    GError *error = NULL;
    PopplerDocument *document = poppler_document_new_from_file(filename, NULL, &error);
    if (error) {
        fprintf(stderr, "Error opening PDF: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }
    return (void *)document; //It is PopplerDocument*
}

// Clean up document (call when done)
void cleanup_pdf_document(void *document) {
    if (document) {
        g_object_unref((PopplerDocument *)document);
    }
}

int render_pdf_page_to_rgba_with_xoffset(void *document, int page_number,
                                        unsigned char *output, int width, int height,
                                        int xoffset) {
    if (!document || !output) { return 0; }

    PopplerPage *page = NULL;
    cairo_surface_t *surface = NULL;
    cairo_t *cr = NULL;
    int actual_height = 0;

    // Get the number of pages
    int num_pages = poppler_document_get_n_pages((PopplerDocument *)document);
    if (page_number < 0 || page_number >= num_pages) {
        fprintf(stderr, "Page number %d out of range (0-%d)\n", page_number, num_pages-1);
        return 0;
    }

    // Get the requested page
    page = poppler_document_get_page((PopplerDocument *)document, page_number);
    if (!page) {
        fprintf(stderr, "Could not load page %d\n", page_number);
        return 0;
    }

    // Get page size and compute scale
    double page_width, page_height;
    poppler_page_get_size(page, &page_width, &page_height);
    if (page_width < 1.0) {
        g_object_unref(page);
        return 0;
    }

    // Calculate zoom factor similar to SVG version
    double zoom_factor = (double)width /((double)xoffset*2 + (double)page_width) ;
    printf("Zoom factor = %f\n", zoom_factor);

    // Create Cairo surface and context
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(surface);

    // Set background (using your SVG background color)
    cairo_set_source_rgba(cr, 228.0/255, 1.0, 235.0/255, 1.0);
    cairo_paint(cr);

    // Apply transformations
    cairo_scale(cr, zoom_factor, zoom_factor);
    cairo_translate(cr, xoffset, 0);

    // Render the page
    poppler_page_render(page, cr);

    // Copy data to output
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    if (stride == width * 4) {
        memcpy(output, data, 4 * width * height);
        actual_height = height;
    } else {
        actual_height = 0; // Unexpected stride
    }

    // Clean up
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(page);
    return actual_height;
}

///////////////////////
#include <string.h>
#include <stdbool.h>

#define STRING_CHUNK_SIZE 4096

typedef struct {
    char *buffer;    // Pointer to dynamic buffer
    size_t capacity; // Total allocated capacity
    size_t length;   // Current string length (excluding null terminator)
} DynamicString;

// Initialize a new DynamicString
static bool string_init(DynamicString *str) {
    str->capacity = STRING_CHUNK_SIZE;
    str->length = 0;
    str->buffer = malloc(str->capacity);
    if (!str->buffer) return false;
    str->buffer[0] = '\0';
    return true;
}

// Append content to DynamicString, expanding if needed
static bool string_append(DynamicString *str, const char *content) {
    size_t content_len = strlen(content);
    size_t new_length = str->length + content_len;

    // Check if we need to expand capacity
    if (new_length + 1 > str->capacity) {  // +1 for null terminator
        size_t new_capacity = str->capacity;
        while (new_capacity <= new_length) {
            new_capacity += STRING_CHUNK_SIZE;
        }

        char *new_buffer = realloc(str->buffer, new_capacity);
        if (!new_buffer) return false;

        str->buffer = new_buffer;
        str->capacity = new_capacity;
    }

    // Append the new content
    memcpy(str->buffer + str->length, content, content_len + 1); // +1 to copy null terminator
    str->length = new_length;
    return true;
}

// Append an integer to DynamicString, expanding if needed
static bool string_append_int(DynamicString *str, int value) {
    // Determine how many characters we'll need (including sign for negative numbers)
    int length = snprintf(NULL, 0, "%d", value);
    if (length < 0) return false; // formatting error

    // Check if we need to expand capacity
    size_t new_length = str->length + length;
    if (new_length + 1 > str->capacity) {  // +1 for null terminator
        size_t new_capacity = str->capacity;
        while (new_capacity <= new_length) {
            new_capacity += STRING_CHUNK_SIZE;
        }

        char *new_buffer = realloc(str->buffer, new_capacity);
        if (!new_buffer) return false;

        str->buffer = new_buffer;
        str->capacity = new_capacity;
    }

    // Format the integer directly into the buffer
    int written = snprintf(str->buffer + str->length, length + 1, "%d", value);
    if (written != length) return false; // formatting error

    str->length = new_length;
    return true;
}

// Free the allocated memory
static void string_free(DynamicString *str) {
    free(str->buffer);
    str->buffer = NULL;
    str->capacity = 0;
    str->length = 0;
}

// Function to get page number from a named destination
static int get_page_number_from_named_dest(void *doc, const char *named_dest) {
    PopplerDest *dest = poppler_document_find_dest((PopplerDocument *)doc, named_dest);
    if (!dest) return -1;

    int page_num = -1;

    if (dest->type == POPPLER_DEST_NAMED) {
        // Handle nested named destinations
        PopplerDest *resolved_dest = poppler_document_find_dest((PopplerDocument *)doc, dest->named_dest);
        if (resolved_dest) {
            page_num = resolved_dest->page_num;
            poppler_dest_free(resolved_dest);
        }
    } else {
        // Direct page number reference
        page_num = dest->page_num;
    }

    poppler_dest_free(dest);
    return page_num;
}

// Build TOC into a DynamicString
static void build_toc_string(void *doc, PopplerIndexIter *iter, int level, DynamicString *result) {
    char line[1024];

    do {
        PopplerAction *action = poppler_index_iter_get_action(iter);
        if (!action) continue;

        if (action->type == POPPLER_ACTION_GOTO_DEST) {
            // Build indentation
            int pos = 0;
            for (int i = 0; i < level; i++) {
                pos += snprintf(line + pos, sizeof(line) - pos, "  ");
            }

            // Add title
            pos += snprintf(line + pos, sizeof(line) - pos, "- %s",
                          action->goto_dest.title ? action->goto_dest.title : "(Untitled)");

            // Add destination info
            if (action->goto_dest.dest) {
                PopplerDest *dest = action->goto_dest.dest;
                if (dest->type == POPPLER_DEST_NAMED) {
                    int page_num = get_page_number_from_named_dest((PopplerDocument* )doc, dest->named_dest);
                    snprintf(line + pos, sizeof(line) - pos, "%s@%d", dest->named_dest, page_num);
                } else {
                    snprintf(line + pos, sizeof(line) - pos, "Bookmark%d", dest->page_num + 1);
                }
            }

            // Add newline and append to result
            strcat(line, "\n");
            string_append(result, line);
        }

        // Process children
        PopplerIndexIter *child = poppler_index_iter_get_child(iter);
        if (child) {
            build_toc_string((PopplerDocument*)doc, child, level + 1, result);
            poppler_index_iter_free(child);
        }

        poppler_action_free(action);
    } while (poppler_index_iter_next(iter));
}

char* get_pdf_toc(void *doc) {
    if (!doc) return NULL;
    // Build TOC
    PopplerIndexIter *toc_iter = poppler_index_iter_new((PopplerDocument *)doc);
    if (!toc_iter) {
        return NULL;
    }
    // Initialize result string
    DynamicString result;
    if (!string_init(&result)) {
        return NULL;
    }
    build_toc_string((PopplerDocument *)doc, toc_iter, 0, &result);
    poppler_index_iter_free(toc_iter);
    return result.buffer;
}

// Function to extract text from a specific page
char* extract_page_text(void *doc, int page_num) {
    // Get the page (0-based index)
    PopplerPage *page = poppler_document_get_page((PopplerDocument *)doc, page_num);
    if (!page) {
        fprintf(stderr, "Error: Page %d not found\n", page_num + 1);
        return NULL;
    }

    // Extract text
    char *text = poppler_page_get_text(page);
    if (!text) {
        fprintf(stderr, "Error: No text found on page %d\n", page_num + 1);
        g_object_unref(page);
        return NULL;
    }

    // Cleanup and return
    g_object_unref(page);
    // If you need standard malloc/free compatibility:
    char *std_text = strdup(text); // Creates malloc-allocated copy
    g_free(text);                  // Free the GLib-allocated string

    return std_text; // Caller can use free() on this
}

char* get_pdf_text_selection(void *doc, int page_num, char* text) {
    if (!text || !doc) { return NULL; }
    DynamicString result_str;
    if (!string_init(&result_str)) { return NULL; }
    PopplerRectangle *rect = NULL;
    bool append_success = true;
    GList* results = NULL;
    PopplerPage *page = poppler_document_get_page((PopplerDocument *)doc, page_num);
    if (!page) {
        string_free(&result_str);
        return NULL;
    }
    results = poppler_page_find_text(page, text);
    if (!results) {
        string_free(&result_str);
        g_object_unref(page);
        return NULL;
    }
    for (GList* entry = results; entry && entry->data; entry = g_list_next(entry)) {
        rect = (PopplerRectangle*)entry->data;
        append_success = string_append(&result_str, "[") &&
                         string_append_int(&result_str, (int)rect->x1) &&
                         string_append(&result_str, ", ") &&
                         string_append_int(&result_str, (int)rect->y1) &&
                         string_append(&result_str, ", ") &&
                         string_append_int(&result_str, (int)rect->x2) &&
                         string_append(&result_str, ", ") &&
                         string_append_int(&result_str, (int)rect->y2) &&
                         string_append(&result_str, "] ");

        if (!append_success) {
            break;
        }
    }
    if (page) { g_object_unref(page); }
    if (results) { g_list_free_full(results, (GDestroyNotify)poppler_rectangle_free); }
    return result_str.buffer; // or return the whole DynamicString as needed
}
