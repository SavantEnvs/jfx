// libFuzzer harness: libxml2 HTML parser (one-shot and push mode) plus HTML serializer.
#include <stddef.h>
#include <stdint.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/xmlIO.h>

static xmlParserInputPtr no_loader(const char *url, const char *id, xmlParserCtxtPtr ctxt) {
    (void)url; (void)id; (void)ctxt;
    return NULL;
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    xmlInitParser();
    xmlSetExternalEntityLoader(no_loader);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    const int options = HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory((const char *)data, (int)size, "noname.html", NULL, options);
    if (doc == NULL) return -1;
    xmlChar *out = NULL;
    int outlen = 0;
    htmlDocDumpMemory(doc, &out, &outlen);
    xmlFree(out);
    xmlFreeDoc(doc);

    htmlParserCtxtPtr ctxt = htmlCreatePushParserCtxt(NULL, NULL, (const char *)data, 0, "noname.html", XML_CHAR_ENCODING_NONE);
    if (ctxt != NULL) {
        htmlCtxtUseOptions(ctxt, options);
        size_t half = size / 2;
        htmlParseChunk(ctxt, (const char *)data, (int)half, 0);
        htmlParseChunk(ctxt, (const char *)data + half, (int)(size - half), 1);
        if (ctxt->myDoc != NULL) xmlFreeDoc(ctxt->myDoc);
        htmlFreeParserCtxt(ctxt);
    }
    return 0;
}
