
/* Copyright (c) 2016-2017, Human Brain Project
 *                          Stefan.Eilemann@epfl.ch
 *                          Daniel.Nachbaur@epfl.ch
 */

#ifndef ZEROEQ_HTTP_REQUESTHANDLER_H
#define ZEROEQ_HTTP_REQUESTHANDLER_H

#include "response.h"

#include <boost/network/protocol/http/server.hpp>
#include <future>
#include <string>

namespace zeroeq
{
namespace http
{

class RequestHandler;
typedef boost::network::http::server< RequestHandler > HTTPServer;

// Contains in/out values for an HTTP request to exchange information between
// cppnetlib and zeroeq::http::Server
struct HTTPRequest
{
    enum class Method
    {
        PUT,
        GET,
        POST,
        PATCH,
        DELETE
    };

    // input from cppnetlib
    Method method;
    std::string url;
    std::string request;

    // output from zeroeq::http::Server
    std::future< Response > response;
};

// The handler class called for each incoming HTTP request from cppnetlib
class RequestHandler
{
public:
    /**
     * @param zmqURL URL to inproc socket for communication between cppnetlib
     *               thread and zeroeq::http::Server thread
     * @param zmqContext the context to create the inproc socket in
     */
    RequestHandler( const std::string& zmqURL, void* zmqContext );

    ~RequestHandler();

    /** Callback for each request from cppnetlib server. */
    void operator() ( const HTTPServer::request& request,
                      HTTPServer::connection_ptr connection );

private:
    void* _socket;
};

}
}

#endif
