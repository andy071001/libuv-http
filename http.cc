
#include "http.h"

namespace http {

  /**
   * `http::Response' implementation
   */

  http_parser_settings settings;

  Events::Events(Listener fn) {
    // on request callback listener
    listener = fn;

    // http parser callback types
    static function<int(http_parser *parser)> on_message_complete;
    static function<int(http_parser *parser, const char *at, size_t len)> on_url;

    // called once a connection has been made and the message is complete.
    on_message_complete = [&](http_parser *parser) -> int {
      Client *client = reinterpret_cast<Client*>(parser->data);
      Request *req = new Request();
      Response *res = new Response();

      req->url = client->url;
      req->method = client->method;

      // Set on end callback
      res->onEnd([&](string str) {
          Client *client = reinterpret_cast<Client*>(parser->data);
          // response buffer
          uv_buf_t resbuf = {
            .base = (char *) str.c_str(),
            .len = str.size()
          };

          // @TODO - this forces us to opt out of streaming writes
          // to the client as 'ended' responses are called with
          // `res.end()' or `res << std::endl'
          uv_write(&client->write_req, (uv_stream_t*) &client->handle, &resbuf, 1,
            [](uv_write_t *req, int status) {
              if (!uv_is_closing((uv_handle_t*) req->handle)) {
                uv_close((uv_handle_t*) req->handle, free_client);
              }
            });
          });

      // pass request to listener

      listener(*req, *res);
      return 0;
    };

    // called after the url has been parsed.
    settings.on_url =
      [](http_parser *parser, const char *at, size_t len) -> int {
        Client *client = static_cast<Client *>(parser->data);
        if (at && client) { client->url = string(at, len); }
        return 0;
      };

    // called when there are either fields or values in the request.
    settings.on_header_field =
      [](http_parser *parser, const char *at, size_t length) -> int {
        return 0;
      };

    // called when header value is given
    settings.on_header_value =
      [](http_parser *parser, const char *at, size_t length) -> int {
        return 0;
      };

    // called once all fields and values have been parsed.
    settings.on_headers_complete =
      [](http_parser *parser) -> int {
        Client *client = static_cast<Client *>(parser->data);
        client->method = string(http_method_str((enum http_method) parser->method));
        return 0;
      };

    // called when there is a body for the request.
    settings.on_body =
      [](http_parser *parser, const char *at, size_t len) -> int {
        Client *client = static_cast<Client *>(parser->data);
        if (at && client) { client->body = string(at, len); }
        return 0;
      };

    // called after all other events.
    settings.on_message_complete =
      [](http_parser *parser) -> int {
        return on_message_complete(parser);
      };
  }

  void Response::onEnd (Buffer<Response>::WriteCallback cb) {
    hasCallback = true;
    write_ = cb;
  }

  void Response::write (string buf) {
    *this << buf;
  }

  void Response::end () {
    *this << std::endl;
  }

  void Response::setHeader (const string key, const string val) {
    headers.insert({ key, val });
  }

  void Response::setStatus (int code) {
    statusCode = code;
  }

  int Response::sync (ostringstream &buf, size_t size) {
    // fail if callback not set
    if (!hasCallback) {
      return 1;
    }

    // write status code and status adjective
    // @TODO - write routine to determine status
    // adjective. 'OK' is hardcoded here..
    buf << "HTTP/1.1 " << statusCode << " OK\r\n";

    // write headers
    for (auto &h: headers) {
      buf << h.first << ": " << h.second << "\r\n";
    }

    // set the content length and content
    buf << "Content-Length: " << size;

    // write body
    buf << "\r\n\r\n";
    return 0;
  }

  /**
   * `Server' implementation
   */

  int Server::listen (const char *ip, int port) {

#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo( &sysinfo );
    int cores = sysinfo.dwNumberOfProcessors;
#else
    int cores = sysconf(_SC_NPROCESSORS_ONLN);
#endif

    std::stringstream cores_string;
    cores_string << cores;

#ifdef _WIN32
    SetEnvironmentVariable("UV_THREADPOOL_SIZE", cores_string);
#else
    setenv("UV_THREADPOOL_SIZE", cores_string.str().c_str(), 1);
#endif
    
    struct sockaddr_in address;
    static function<void(uv_stream_t *socket, int status)> on_connect;
    static function<void(uv_stream_t *tcp, ssize_t nread, const uv_buf_t *buf)> read;

    UV_LOOP = uv_default_loop();
    uv_tcp_init(UV_LOOP, &socket_);

    //
    // @TODO - Not sure exactly how to use this,
    // after the initial timeout, it just
    // seems to kill the server.
    //
    //uv_tcp_keepalive(&socket_,1,60);

    uv_ip4_addr(ip, port, &address);
    uv_tcp_bind(&socket_, (const struct sockaddr*) &address, 0);

    // called once a connection is made.
    on_connect = [&](uv_stream_t *handle, int status) {
      Client *client = new Client();

      // init tcp handle
      uv_tcp_init(UV_LOOP, &client->handle);

      // init http parser
      http_parser_init(&client->parser, HTTP_REQUEST);

      // client reference for parser routines
      client->parser.data = client;

      // client reference for handle data on requests
      client->handle.data = client;

      // accept connection passing in refernce to the client handle
      uv_accept(handle, (uv_stream_t*) &client->handle);

      // called for every read
      read = [&](uv_stream_t *tcp, ssize_t nread, const uv_buf_t *buf) {
        ssize_t parsed;
        Client *client = static_cast<Client *>(tcp->data);

        if (nread >= 0) {
          parsed = (ssize_t) http_parser_execute(&client->parser,
                                                 &settings,
                                                 buf->base,
                                                 nread);

          // close handle
          if (parsed < nread) {
            uv_close((uv_handle_t*) &client->handle, free_client);
          }
        } else {
          if (nread != UV_EOF) {
            // @TODO - debug error
          }

          // close handle
          uv_close((uv_handle_t*) &client->handle, free_client);
        }

        // free request buffer data
        free(buf->base);
      };

      // allocate memory and attempt to read.
      uv_read_start((uv_stream_t*) &client->handle,
          // allocator
          [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
            *buf = uv_buf_init((char*) malloc(suggested_size), suggested_size);
          },

          // reader
          [](uv_stream_t *tcp, ssize_t nread, const uv_buf_t *buf) {
            read(tcp, nread, buf);
          });
    };

    uv_listen((uv_stream_t*) &socket_, MAX_WRITE_HANDLES,
        // listener
        [](uv_stream_t *socket, int status) {
          on_connect(socket, status);
        });

    // init loop
    uv_run(UV_LOOP, UV_RUN_DEFAULT);
    return 0;
  }
}
