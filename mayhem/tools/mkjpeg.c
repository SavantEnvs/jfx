// Dev-time tool (not run by build.sh): writes the committed JPEG fixtures/seeds with the bundled libjpeg.
//   mkjpeg <out.jpg> <w> <h> <rgb|gray|cmyk|ycck> <h2v2|h1v1|h2v1> <quality> [icc]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

static unsigned char *obuf; static size_t olen, ocap;
static void init_dest(j_compress_ptr c) { ocap = 1 << 16; obuf = malloc(ocap); c->dest->next_output_byte = obuf; c->dest->free_in_buffer = ocap; }
static boolean empty_out(j_compress_ptr c) { size_t o = ocap; ocap *= 2; obuf = realloc(obuf, ocap); c->dest->next_output_byte = obuf + o; c->dest->free_in_buffer = ocap - o; return TRUE; }
static void term_dest(j_compress_ptr c) { olen = ocap - c->dest->free_in_buffer; }

int main(int argc, char **argv) {
    if (argc < 7) return 2;
    int w = atoi(argv[2]), h = atoi(argv[3]), q = atoi(argv[6]);
    int nc = !strcmp(argv[4], "gray") ? 1 : (!strcmp(argv[4], "rgb") ? 3 : 4);
    struct jpeg_compress_struct ci; struct jpeg_error_mgr je; struct jpeg_destination_mgr d;
    ci.err = jpeg_std_error(&je);
    jpeg_create_compress(&ci);
    d.init_destination = init_dest; d.empty_output_buffer = empty_out; d.term_destination = term_dest;
    ci.dest = &d;
    ci.image_width = w; ci.image_height = h; ci.input_components = nc;
    ci.in_color_space = nc == 1 ? JCS_GRAYSCALE : (nc == 3 ? JCS_RGB : JCS_CMYK);
    jpeg_set_defaults(&ci);
    if (!strcmp(argv[4], "ycck")) jpeg_set_colorspace(&ci, JCS_YCCK);
    jpeg_set_quality(&ci, q, TRUE);
    if (nc >= 3) {
        int hs = !strcmp(argv[5], "h1v1") ? 1 : 2, vs = !strcmp(argv[5], "h2v2") ? 2 : 1;
        ci.comp_info[0].h_samp_factor = hs; ci.comp_info[0].v_samp_factor = vs;
    }
    jpeg_start_compress(&ci, TRUE);
    if (argc > 7) {
        unsigned char icc[40] = "ICC_PROFILE";
        icc[12] = 1; icc[13] = 1;  /* seq 1 of 1, then a dummy 26-byte payload */
        for (int i = 14; i < 40; i++) icc[i] = (unsigned char)i;
        jpeg_write_marker(&ci, JPEG_APP0 + 2, icc, sizeof icc);
    }
    unsigned char *row = malloc((size_t)w * nc);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            for (int c = 0; c < nc; c++)
                row[x * nc + c] = (unsigned char)((x * 255 / (w > 1 ? w - 1 : 1)) * (c == 0) + (y * 255 / (h > 1 ? h - 1 : 1)) * (c == 1) + ((x ^ y) * 16) * (c >= 2));
        JSAMPROW rp[1] = { row };
        jpeg_write_scanlines(&ci, rp, 1);
    }
    jpeg_finish_compress(&ci);
    FILE *f = fopen(argv[1], "wb"); fwrite(obuf, 1, olen, f); fclose(f);
    return 0;
}
