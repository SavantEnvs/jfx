// libFuzzer harness: libxml2 XPath compiler/evaluator over a fixed document; the input is the expression.
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

static const char kDoc[] =
    "<?xml version=\"1.0\"?>"
    "<root xmlns:a=\"urn:a\" id=\"r\"><a:item n=\"1\">one</a:item><a:item n=\"2\">two</a:item>"
    "<list><i>10</i><i>20</i><i>30</i></list><!-- c --><?pi data?><t>text &amp; more</t></root>";

static xmlDocPtr doc;

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    xmlInitParser();
    doc = xmlReadMemory(kDoc, (int)sizeof(kDoc) - 1, "fixed.xml", NULL, XML_PARSE_NONET);
    return doc == NULL ? 1 : 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    char *expr = malloc(size + 1);
    memcpy(expr, data, size);
    expr[size] = 0;

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    xmlXPathRegisterNs(ctx, BAD_CAST "a", BAD_CAST "urn:a");
    xmlXPathCompExprPtr comp = xmlXPathCtxtCompile(ctx, BAD_CAST expr);
    int ret = -1;
    if (comp != NULL) {
        xmlXPathObjectPtr res = xmlXPathCompiledEval(comp, ctx);
        if (res != NULL) {
            xmlXPathCastToString(res);
            xmlXPathFreeObject(res);
            ret = 0;
        }
        xmlXPathFreeCompExpr(comp);
    }
    xmlXPathFreeContext(ctx);
    free(expr);
    return ret;
}
