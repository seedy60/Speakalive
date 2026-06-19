/* webread.h - fetch a web page and reduce it to readable text. */
#ifndef SPEAKALIVE_WEBREAD_H
#define SPEAKALIVE_WEBREAD_H

/* Download 'url' (HTTP or HTTPS), strip the HTML to plain readable text and
 * return it as a heap ANSI string (free with Mem_Free).  Returns NULL on a
 * network/parse failure.  Lines are separated with CR/LF for the edit box. */
char *Web_FetchText(const char *url);

#endif /* SPEAKALIVE_WEBREAD_H */
