// Known-answer probe over the same bundled libraries the fuzz targets exercise, built with the project's
// NORMAL flags (no sanitizers). test.sh runs it and compares its stdout against golden values.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include <libxml/parser.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxslt/xslt.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

static unsigned char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n + 1);
    if (fread(b, 1, *n, f) != *n) exit(2);
    b[*n] = 0; fclose(f);
    return b;
}

static void init_source(j_decompress_ptr c) { (void)c; }
static boolean fill(j_decompress_ptr c) { static const JOCTET e[2] = {0xFF, JPEG_EOI}; c->src->next_input_byte = e; c->src->bytes_in_buffer = 2; return TRUE; }
static void skip(j_decompress_ptr c, long n) { if (n > 0) { c->src->next_input_byte += n; c->src->bytes_in_buffer -= (size_t)n; } }
static void term(j_decompress_ptr c) { (void)c; }

static int do_jpeg(const char *path) {
    size_t n; unsigned char *buf = slurp(path, &n);
    struct jpeg_decompress_struct ci; struct jpeg_error_mgr je; struct jpeg_source_mgr s;
    ci.err = jpeg_std_error(&je);
    jpeg_create_decompress(&ci);
    s.next_input_byte = buf; s.bytes_in_buffer = n; s.init_source = init_source; s.fill_input_buffer = fill;
    s.skip_input_data = skip; s.resync_to_restart = jpeg_resync_to_restart; s.term_source = term;
    ci.src = &s;
    jpeg_save_markers(&ci, JPEG_APP0 + 2, 0xFFFF);
    jpeg_read_header(&ci, TRUE);
    int icc = ci.marker_list != NULL && ci.marker_list->marker == JPEG_APP0 + 2;
    int in_comps = ci.num_components;
    jpeg_start_decompress(&ci);
    size_t stride = (size_t)ci.output_width * ci.output_components;
    unsigned char *row = malloc(stride);
    JSAMPROW rows[1] = { row };
    unsigned long sum = 0;
    while (ci.output_scanline < ci.output_height) {
        jpeg_read_scanlines(&ci, rows, 1);
        for (size_t i = 0; i < stride; i++) sum += row[i];
    }
    printf("jpeg %ux%u in=%d out=%d icc=%d sum=%lu\n", ci.output_width, ci.output_height, in_comps, ci.output_components, icc, sum);
    jpeg_finish_decompress(&ci);
    jpeg_destroy_decompress(&ci);
    return 0;
}

static int do_xml(const char *path) {
    size_t n; unsigned char *buf = slurp(path, &n);
    xmlDocPtr d = xmlReadMemory((char *)buf, (int)n, "k.xml", NULL, XML_PARSE_NONET | XML_PARSE_NOENT);
    if (!d) { printf("xml parse-failed\n"); return 1; }
    xmlXPathContextPtr cx = xmlXPathNewContext(d);
    xmlXPathRegisterNs(cx, BAD_CAST "a", BAD_CAST "urn:a");
    const char *exprs[] = { "count(//*)", "string(/root/@id)", "sum(//list/i)", "string(//a:item[@n='2'])", "count(//a:item)", "string(/root/t)" };
    for (size_t i = 0; i < sizeof exprs / sizeof *exprs; i++) {
        xmlXPathObjectPtr r = xmlXPathEvalExpression(BAD_CAST exprs[i], cx);
        xmlChar *s = r ? xmlXPathCastToString(r) : NULL;
        printf("xpath %s = %s\n", exprs[i], s ? (char *)s : "(null)");
        xmlFree(s); xmlXPathFreeObject(r);
    }
    xmlChar *out; int len;
    xmlDocDumpMemory(d, &out, &len);
    printf("xml-dump-bytes %d\n", len);
    xmlFree(out);
    xmlXPathFreeContext(cx); xmlFreeDoc(d);
    return 0;
}

static int do_html(const char *path) {
    size_t n; unsigned char *buf = slurp(path, &n);
    htmlDocPtr d = htmlReadMemory((char *)buf, (int)n, "k.html", NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
    if (!d) { printf("html parse-failed\n"); return 1; }
    xmlChar *out; int len;
    htmlDocDumpMemory(d, &out, &len);
    printf("html-dump:\n%s", (char *)out);
    xmlFree(out); xmlFreeDoc(d);
    return 0;
}

static int do_xslt(const char *style_path, const char *src_path) {
    size_t n1, n2; unsigned char *b1 = slurp(style_path, &n1), *b2 = slurp(src_path, &n2);
    xmlDocPtr sd = xmlReadMemory((char *)b1, (int)n1, "s.xsl", NULL, XML_PARSE_NONET);
    xsltStylesheetPtr st = xsltParseStylesheetDoc(sd);
    xmlDocPtr src = xmlReadMemory((char *)b2, (int)n2, "s.xml", NULL, XML_PARSE_NONET);
    if (!st || !src) { printf("xslt setup-failed\n"); return 1; }
    xmlDocPtr res = xsltApplyStylesheet(st, src, NULL);
    if (!res) { printf("xslt apply-failed\n"); return 1; }
    xmlChar *out; int len;
    xsltSaveResultToString(&out, &len, res, st);
    printf("xslt-result:%s", (char *)out);
    xmlFree(out); xmlFreeDoc(res); xmlFreeDoc(src); xsltFreeStylesheet(st);
    return 0;
}

int main(int argc, char **argv) {
    xmlInitParser();
    if (argc >= 3 && !strcmp(argv[1], "jpeg")) return do_jpeg(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "xml")) return do_xml(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "html")) return do_html(argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "xslt")) return do_xslt(argv[2], argv[3]);
    fprintf(stderr, "usage: kat jpeg|xml|html <file> | xslt <style> <src>\n");
    return 2;
}
