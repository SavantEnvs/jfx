// File-input fuzz target: decode a JPEG with the libjpeg copy bundled in javafx.graphics (what
// com.sun.javafx.iio.jpeg.JPEGImageLoader drives). libjpeg reports errors through error_exit(), which must
// not return; the default handler exits the process, so this is a file-input target (each input is its own
// process) rather than an in-process libFuzzer harness.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

static void init_source(j_decompress_ptr c) { (void)c; }
static boolean fill_input_buffer(j_decompress_ptr c) {
    static const JOCTET eoi[2] = { 0xFF, JPEG_EOI };
    c->src->next_input_byte = eoi;
    c->src->bytes_in_buffer = 2;
    return TRUE;
}
static void skip_input_data(j_decompress_ptr c, long n) {
    if (n <= 0) return;
    if ((size_t)n > c->src->bytes_in_buffer) n = (long)c->src->bytes_in_buffer;
    c->src->next_input_byte += n;
    c->src->bytes_in_buffer -= (size_t)n;
}
static void term_source(j_decompress_ptr c) { (void)c; }

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 1; }
    unsigned char *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return 2; }
    fclose(f);

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct jpeg_source_mgr src;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    src.next_input_byte = buf;
    src.bytes_in_buffer = (size_t)n;
    src.init_source = init_source;
    src.fill_input_buffer = fill_input_buffer;
    src.skip_input_data = skip_input_data;
    src.resync_to_restart = jpeg_resync_to_restart;
    src.term_source = term_source;
    cinfo.src = &src;
    jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF);  /* ICC profile, as JPEGImageLoader does */

    jpeg_read_header(&cinfo, TRUE);
    if (cinfo.jpeg_color_space == JCS_YCCK || cinfo.jpeg_color_space == JCS_CMYK)
        cinfo.out_color_space = JCS_CMYK;
    else if (cinfo.num_components == 1)
        cinfo.out_color_space = JCS_GRAYSCALE;
    else
        cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    size_t stride = (size_t)cinfo.output_width * cinfo.output_components;
    unsigned char *row = malloc(stride ? stride : 1);
    JSAMPROW rows[1] = { row };
    unsigned long sum = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, rows, 1);
        for (size_t i = 0; i < stride; i += 61) sum += row[i];
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    free(row);
    free(buf);
    return (int)(sum & 0) ;
}
