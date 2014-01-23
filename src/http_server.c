/* ympd
   (c) 2013-2014 Andrew Karpow <andy@ympd.org>
   This project's homepage is: http://www.ympd.org

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <libwebsockets.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <mpd/client.h>

#include "http_server.h"
#include "mpd_client.h"
#include "config.h"

char *resource_path = LOCAL_RESOURCE_PATH;
extern enum mpd_conn_states mpd_conn_state;

struct serveable {
    const char *urlpath;
    const char *mimetype;
}; 

static const struct serveable whitelist[] = {
    { "/css/bootstrap.css", "text/css" },
    { "/css/mpd.css", "text/css" },

    { "/js/bootstrap.min.js", "text/javascript" },
    { "/js/mpd.js", "text/javascript" },
    { "/js/jquery-1.10.2.min.js", "text/javascript" },
    { "/js/jquery.cookie.js", "text/javascript" },
    { "/js/bootstrap-slider.js", "text/javascript" },
    { "/js/bootstrap-notify.js", "text/javascript" },
    { "/js/sammy.js", "text/javascript" },

    { "/fonts/glyphicons-halflings-regular.woff", "application/x-font-woff"},
    { "/fonts/glyphicons-halflings-regular.svg", "image/svg+xml"},
    { "/fonts/glyphicons-halflings-regular.ttf", "application/x-font-ttf"},
    { "/fonts/glyphicons-halflings-regular.eot", "application/vnd.ms-fontobject"},

    { "/assets/favicon.ico", "image/vnd.microsoft.icon" },

    /* last one is the default served if no match */
    { "/index.html", "text/html" },
};

static const char http_header[] = "HTTP/1.0 200 OK\x0d\x0a"
                                  "Server: libwebsockets\x0d\x0a"
                                  "Content-Type: application/json\x0d\x0a"
                                  "Content-Length: 000000\x0d\x0a\x0d\x0a";

/* Converts a hex character to its integer value */
char from_hex(char ch) {
    return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Returns a url-decoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_decode(char *str) {
    char *pstr = str, *buf = malloc(strlen(str) + 1), *pbuf = buf;
    while (*pstr) {
        if (*pstr == '%') {
            if (pstr[1] && pstr[2]) {
                *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
                pstr += 2;
            }
        } else if (*pstr == '+') {
            *pbuf++ = ' ';
        } else {
            *pbuf++ = *pstr;
        }
        pstr++;
    }
    *pbuf = '\0';
    return buf;
}

int callback_http(struct libwebsocket_context *context,
        struct libwebsocket *wsi,
        enum libwebsocket_callback_reasons reason, void *user,
        void *in, size_t len)
{
    char *response_buffer, *p;
    char buf[64];
    size_t n, response_size = 0;

    switch (reason) {
        case LWS_CALLBACK_HTTP:
            if(in && strncmp((const char *)in, "/api/", 5) == 0)
            {

                p = (char *)malloc(MAX_SIZE + 100);
                memcpy(p, http_header, sizeof(http_header) - 1);
                response_buffer = p + sizeof(http_header) - 1;

                /* put content length and payload to buffer */
                if(mpd_conn_state != MPD_CONNECTED) {}
                else if(strncmp((const char *)in, "/api/get_browse", 15) == 0)
                {
                    char *url;
                    if(sscanf(in, "/api/get_browse/%m[^\t\n]", &url) == 1)
                    {
                        char *url_decoded = url_decode(url);
                        response_size = mpd_put_browse(response_buffer, url_decoded);
                        free(url_decoded);
                        free(url);
                    }
                    else
                        response_size = mpd_put_browse(response_buffer, "/");

                }
                else if(strncmp((const char *)in, "/api/get_playlist", 17)  == 0)
                    response_size = mpd_put_playlist(response_buffer);
                else if(strncmp((const char *)in, "/api/get_version", 16)  == 0)
                    response_size = snprintf(response_buffer, MAX_SIZE,
                            "{\"type\":\"version\",\"data\":{"
                            "\"ympd_version\":\"%d.%d.%d\","
                            "\"mpd_version\":\"%d.%d.%d\""
                            "}}",
                            YMPD_VERSION_MAJOR, YMPD_VERSION_MINOR, YMPD_VERSION_PATCH,
                            LIBMPDCLIENT_MAJOR_VERSION, LIBMPDCLIENT_MINOR_VERSION,
                            LIBMPDCLIENT_PATCH_VERSION);
                else if(strncmp((const char *)in, "/api/get_lists", 14)  == 0)
                {
                    char *url;
                    if(sscanf(in, "/api/get_lists/%m[^\t\n]", &url) == 1)
                    {
                        char *url_decoded = url_decode(url);
                        response_size = mpd_put_list_content(response_buffer, url_decoded);
                        free(url_decoded);
                        free(url);
                    }
                    else
                        response_size = mpd_put_lists(response_buffer);
                }

                /* Copy size to content-length field */
                sprintf(buf, "%6zu", response_size);
                memcpy(p + sizeof(http_header) - 11, buf, 6);

                n = libwebsocket_write(wsi, (unsigned char *)p,
                        sizeof(http_header) - 1 + response_size, LWS_WRITE_HTTP);

                free(p);
                /*
                 * book us a LWS_CALLBACK_HTTP_WRITEABLE callback
                 */
                libwebsocket_callback_on_writable(context, wsi);

            }
            else
            {            
                for (n = 0; n < (sizeof(whitelist) / sizeof(whitelist[0]) - 1); n++)
                    if (in && strcmp((const char *)in, whitelist[n].urlpath) == 0)
                        break;

                sprintf(buf, "%s%s", resource_path, whitelist[n].urlpath);

                if (libwebsockets_serve_http_file(context, wsi, buf, whitelist[n].mimetype, NULL))
                    return -1; /* through completion or error, close the socket */
            }
            break;

        case LWS_CALLBACK_HTTP_FILE_COMPLETION:
            /* kill the connection after we sent one file */
            return -1;
        default:
            break;
    }

    return 0;
}
