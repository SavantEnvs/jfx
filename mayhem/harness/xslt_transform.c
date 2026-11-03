// libFuzzer harness: libxslt (the copy WebKit/JavaFX bundles) — the input is a stylesheet, compiled and
// applied to a fixed source document with every file/network/write capability denied.
#include <stddef.h>
#include <stdint.h>
#include <libxml/parser.h>
#include <libxml/xmlIO.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/security.h>
#include <libxslt/xsltutils.h>

static const char kDoc[] =
    "<?xml version=\"1.0\"?>"
    "<catalog><book id=\"b1\"><title>Alpha</title><price>12</price></book>"
    "<book id=\"b2\"><title>Beta</title><price>7</price></book></catalog>";

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
    xmlDocPtr sdoc = xmlReadMemory((const char *)data, (int)size, "style.xsl", NULL, XML_PARSE_NONET | XML_PARSE_NOENT);
    if (sdoc == NULL) return -1;
    xsltStylesheetPtr style = xsltParseStylesheetDoc(sdoc);
    if (style == NULL) {
        xmlFreeDoc(sdoc);
        return -1;
    }
    xmlDocPtr src = xmlReadMemory(kDoc, (int)sizeof(kDoc) - 1, "src.xml", NULL, XML_PARSE_NONET);
    xsltTransformContextPtr ctxt = xsltNewTransformContext(style, src);
    xsltSecurityPrefsPtr sec = xsltNewSecurityPrefs();
    xsltSetSecurityPrefs(sec, XSLT_SECPREF_READ_FILE, xsltSecurityForbid);
    xsltSetSecurityPrefs(sec, XSLT_SECPREF_WRITE_FILE, xsltSecurityForbid);
    xsltSetSecurityPrefs(sec, XSLT_SECPREF_CREATE_DIRECTORY, xsltSecurityForbid);
    xsltSetSecurityPrefs(sec, XSLT_SECPREF_READ_NETWORK, xsltSecurityForbid);
    xsltSetSecurityPrefs(sec, XSLT_SECPREF_WRITE_NETWORK, xsltSecurityForbid);
    xsltSetCtxtSecurityPrefs(sec, ctxt);
    xmlDocPtr res = xsltApplyStylesheetUser(style, src, NULL, NULL, NULL, ctxt);
    if (res != NULL) {
        xmlChar *out = NULL;
        int outlen = 0;
        xsltSaveResultToString(&out, &outlen, res, style);
        xmlFree(out);
        xmlFreeDoc(res);
    }
    xsltFreeTransformContext(ctxt);
    xsltFreeSecurityPrefs(sec);
    xmlFreeDoc(src);
    xsltFreeStylesheet(style);
    return 0;
}
