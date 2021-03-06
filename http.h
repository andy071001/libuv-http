#ifndef HTTP_UV_H
#define HTTP_UV_H

#include <map>
#include <string>
#include <sstream>
#include <iostream>
#include <unistd.h>
#include <functional>

#include "uv.h"
#include "http_parser.h"

#ifndef _WIN32
#include <unistd.h>
#endif

extern "C" {
#include "uv.h"
#include "http_parser.h"
}

#define MAX_WRITE_HANDLES 1000

#ifndef HTTP_UV_LOOP
#define HTTP_UV_LOOP
static uv_loop_t *UV_LOOP;
#endif

namespace http {

  using namespace std;
  static void free_client (uv_handle_t *);

  /**
   * `http' api
   */

  template <class Type> class Buffer;
  template <class Type> class IStream;
  class Response;
  class Request;
  class Client;
  class Server;

  /**
   * `Baton' class
   */

  class Baton {
    public:
      Client *client;
      string result;
      uv_work_t request;
      bool error;
  };

  /**
   * `Buffer' class
   *
   * Extends `std::stringbuf'
   */

  template <class Type> class Buffer : public stringbuf {

    friend class Request;
    friend class Response;

    /**
     * Pointer to buffer stream
     */

    Type *stream_;

    public:

    /**
     * Write callback type attached to `stream_'
     */

    typedef function<void (string s)> WriteCallback;

    /**
     * `Buffer<Type>' constructor
     */

    Buffer<Type> (ostream &str) {};

    /**
     * `Buffer<Type>' destructor
     */

    ~Buffer () {};

    /**
     * Implements the `std::streambuf::sync()' virtual
     * method. Returns `0' on success, otherwise `1' on
     * failure.
     */

    int sync () {
      // create out stream
      string out = str();

      // write buffer stream
      std::ostringstream buf;

      // sync
      if (1 == stream_->sync(buf, out.size())) {
        return 1;
      }

      // concat
      buf << out;

      // write
      out = buf.str();

      // defer to write callback
      stream_->write_(out);
      return 0;
    }
  };

  /**
   * `IStream' interface
   */

  template <class Type> class IStream : virtual public ostream {

    protected:

      /**
       * `IStream' constructor
       */

    public:
      IStream () { };

      void write (string);
      void read () {};
      void end () {};
  };

  /**
   * `Request' class
   */

  class Request {
    public:

      /**
       * Request URL
       */

      string url;

      /**
       * Request HTTP method
       */

      string method;

      /**
       * Request HTTP status code
       */

      string status_code;

      /**
       * Request body
       */

      string body;

      /**
       * Request headers map
       */

      map<const string, const string> headers;
  };

  /**
   * `Client' class
   *
   * Extends `http::Request'
   */

  class Client : public Request {
    public:
      /**
       * uv tcp socket handle
       */

      uv_tcp_t handle;

      /**
       * http parser instance
       */

      http_parser parser;

      /**
       * uv write worker
       */

      uv_write_t write_req;
  };

  /**
   * `Response' stream class
   *
   * Extends `std::ostream'
   */

  class Response : public IStream<Response> {

    friend class Buffer<class Response>;

    private:

      /**
       * Response buffer
       */

      Buffer<Response> buffer_;

      /**
       * `Buffer' string stream
       */

      stringstream stream_;

      /**
       * Buffer write implementation
       */

      Buffer<Response>::WriteCallback write_;

    protected:

      /**
       * Stream sync interface
       */

      virtual int sync (ostringstream &, size_t);

    public:

      /**
       * HTTP response status code
       */

      int statusCode = 200;

      /**
       * HTTP response status adjective
       */

      string statusAdjective;

      /**
       * Key to value map of response headers
       */

      map<const string, const string> headers;

      /**
       * `Buffer::WriteCallback' detect predicate
       */

      bool hasCallback = false;

      /**
       * Sets a key value pair as a HTTP
       * header
       */

      void setHeader (const string, const string);

      /**
       * Sets the HTTP status code
       *
       * @TODO - handle `statusAdjective'
       */

      void setStatus (int);

      /**
       * Sets a `Buffer::WriteCallback write_' routine for when
       * the response has been sent
       */

      void onEnd (Buffer<Response>::WriteCallback);

      /**
       * Write to buffer stream
       */

      void write (string);

      /**
       * End write stream
       */

      void end ();

      /**
       * `Response' constructor
       *
       * Initializes super class and `Buffer buffer_'
       * instance with `stringstream stream_'
       */

      Response () : IStream(), ostream(&buffer_), buffer_(stream_) {
        buffer_.stream_ = this;
      }

      /**
       * `Response' destructor
       */

      ~Response() { };
  };

  /**
   * `Events' class
   */

  class Events {

    public:

      /**
       * `Listener' callback
       */

      typedef function<void (Request &, Response &)> Listener;

      /**
       * HTTP event listener callback
       */

      Listener listener;

      /**
       * `Events' constructor
       */

      Events(Listener fn);
  };

  /**
   * `Server' class
   */

  class Server {

    protected:

      /**
       * Internal uv tcp socket
       */

      uv_tcp_t socket_;

      /**
       * Internal server events
       */

      Events events_;

    public:

      /**
       * `Server' constructor
       */

      Server (Events::Listener listener) : events_(listener) {}

      /**
       * Called by the user when they want to start listening for
       * connections. starts the main event loop, provides parser, etc.
       */

      int listen (const char *, int);
  };

  static void free_client (uv_handle_t *handle) {
    auto *client = reinterpret_cast<Client*>(handle->data);
    free(client);
  }

} // namespace http

#endif

