// libFuzzer harness: libxml2 (the copy WebKit/JavaFX bundles) XML parser, xmlreader and serializer.
// First input byte selects parser options; the rest is the document.
#include <stddef.h>
#include <stdint.h>
#include <libxml/parser.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlIO.h>
#include <libxml/tree.h>

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
    if (size < 2) return -1;
    static const int opts[] = { XML_PARSE_RECOVER, XML_PARSE_NOENT, XML_PARSE_DTDLOAD, XML_PARSE_DTDATTR,
                                XML_PARSE_DTDVALID, XML_PARSE_NOBLANKS, XML_PARSE_NOCDATA, XML_PARSE_NSCLEAN };
    int options = XML_PARSE_NONET;
    for (int i = 0; i < 8; i++)
        if (data[0] & (1u << i)) options |= opts[i];
    const char *xml = (const char *)data + 1;
    int len = (int)(size - 1);

    xmlDocPtr doc = xmlReadMemory(xml, len, "noname.xml", NULL, options);
    if (doc == NULL) return -1;
    xmlChar *out = NULL;
    int outlen = 0;
    xmlDocDumpMemory(doc, &out, &outlen);
    xmlFree(out);
    xmlFreeDoc(doc);

    xmlTextReaderPtr reader = xmlReaderForMemory(xml, len, "noname.xml", NULL, options);
    if (reader != NULL) {
        while (xmlTextReaderRead(reader) == 1) {
            xmlChar *v = xmlTextReaderReadString(reader);
            xmlFree(v);
            (void)xmlTextReaderNodeType(reader);
        }
        xmlFreeTextReader(reader);
    }
    return 0;
}
