void* init_pdf_document(const char *filename);
void cleanup_pdf_document(void *document);
int render_pdf_page_to_rgba_with_xoffset(void *document, int page_number, unsigned char *output, int width, int height, int xoffset);
char* get_pdf_toc(void *doc);
char* extract_page_text(void *doc, int page_num);
char* get_pdf_text_selection(void *doc, int page_num, char* text);
