
/* Copyright (c) 2016, Human Brain Project
 *                     Stefan.Eilemann@epfl.ch
 */

#ifndef ZEROEQ_HTTP_SERVER_H
#define ZEROEQ_HTTP_SERVER_H

#include "response.h"

#include <zeroeq/http/api.h>
#include <zeroeq/receiver.h> // base class
#include <zeroeq/log.h>

namespace zeroeq
{
/** HTTP protocol support. */
namespace http
{

/**
 * HTTP REST callback with payload, returning a Response future.
 * The string is the request's payload or a query string of a GET request
 */
using RESTFunc = std::function< std::future< Response >( const std::string& ) >;

/**
 * HTTP REST callback for a given path returning a Response future.
 * The first string provides the url part after the registered endpoint:
 * "api/windows/jf321f" -> "jf321f".
 * The second is the request's payload or a query string of a GET request
 * If the string matches the registered endpoint pass empty string as
 * path: "api/windows/ -> ""
 */
using RESTPathFunc = std::function< std::future< Response >( const std::string&,
                                                         const std::string& ) >;

/**
 * Serves HTTP GET and PUT requests for servus::Serializable objects.
 *
 * Behaves semantically like a Publisher (for GET) and Subscriber (for PUT),
 * except uses HTTP with JSON payload as the protocol. Requests are served
 * synchronously (as per HTTP spec). Objects are available under their
 * Serializable::getTypeName(), with '::' replaced by '/'. The REST API is case
 * insensitive. For example, zerobuf::render::Camera is served at
 * 'GET|PUT [uri]/zerobuf/render/camera'.
 *
 * Not thread safe.
 *
 * Example: @include tests/http/server.cpp
 */
class Server : public zeroeq::Receiver
{
public:
    /** @name Setup */
    //@{
    /**
     * Construct a new HTTP server.
     *
     * To process requests on the incoming port, call receive(). If no hostname
     * is given, the server listens on all interfaces (INADDR_ANY). If no port
     * is given, the server selects a random port. Use getURI() to retrieve the
     * chosen parameters.
     *
     * @param uri The server address in the form "[tcp://][hostname][:port]"
     * @param shared a shared receiver, see Receiver constructor.
     * @throw std::runtime_error on malformed URI or connection issues.
     */
    ZEROEQHTTP_API Server( const URI& uri, Receiver& shared );
    ZEROEQHTTP_API explicit Server( const URI& uri );
    ZEROEQHTTP_API explicit Server( Receiver& shared );
    ZEROEQHTTP_API Server();
    ZEROEQHTTP_API explicit Server( Server& shared )
        : Server( static_cast< Receiver& >( shared )) {}
    ZEROEQHTTP_API ~Server();

    /**
     * Create a new Server when requested.
     *
     * The creation and parameters depend on the following command line
     * parameters:
     * * --zeroeq-http-server [host][:port]: Enable the server. The optional
     *   parameters configure the web server, running by default on INADDR_ANY
     *   and a randomly chosen port
     */
    ZEROEQHTTP_API
    static std::unique_ptr< Server > parse( int argc, const char* const* argv);
    ZEROEQHTTP_API
    static std::unique_ptr< Server > parse( int argc, const char* const* argv,
                                            Receiver& shared );
    /**
     * Get the publisher URI.
     *
     * Contains the used hostname and port, if none where given in the
     * constructor uri.
     *
     * @return the publisher URI.
     */
    ZEROEQHTTP_API const URI& getURI() const;

    /**
     * Get the underlying socket descriptor.
     *
     * Can be used by client code to be notified when new data is available and
     * subsequently call receive.
     *
     * @return the socket descriptor.
     * @throw std::runtime_error if the descriptor could not be obtained.
     * @note not supported on Windows due to ZMQ limitations, will throw
     *       std::runtime_error
     */
    ZEROEQHTTP_API SocketDescriptor getSocketDescriptor() const;
    //@}

    /** Handle a single action on a given endpoint. */
    bool handle( zeroeq::http::Verb action, const std::string& endpoint,
                 zeroeq::http::RESTFunc func );

    /** Handle a single action on a given endpoint derived from the path . */
    bool handlePath( zeroeq::http::Verb action, const std::string& endpoint,
                     zeroeq::http::RESTPathFunc func );

    /** @name Object registration for PUT and GET requests */
    //@{
    /**
     * Handle PUT and GET for the given object.
     *
     * @param object the object to update and serve on receive()
     * @return true if subscription was successful, false otherwise
     */
    bool handle( servus::Serializable& object )
        { return handlePUT( object ) && handleGET( object ); }

    /**
     * @overload
     * @param object the object to update and serve on receive()
     * @param endpoint use this as the URL endpoint instead of the default
     *                 servus::Serializable::getTypeName()
     */
    ZEROEQHTTP_API bool handle( const std::string& endpoint,
                                servus::Serializable& object );

    /** Remove PUT and GET handling for given object. */
    ZEROEQHTTP_API bool remove( const servus::Serializable& object );

    /** Remove PUT and GET handling for given endpoint. */
    ZEROEQHTTP_API bool remove( const std::string& endpoint );

    /**
     * Subscribe a serializable object to receive updates from HTTP PUT
     * requests.
     *
     * Every update will be directly applied on the object during receive()
     * using fromJSON(). To track updates on the object, the serializable's
     * updated function is called accordingly.
     *
     * The subscribed object instance has to be valid until removePUT().
     *
     * @param object the object to update on receive()
     * @return true if subscription was successful, false otherwise
     */
    ZEROEQHTTP_API bool handlePUT( servus::Serializable& object );

    /**
     * @overload
     * @param object the object to update on receive()
     * @param endpoint use this as the URL endpoint instead of the default
     *                 servus::Serializable::getTypeName()
     */
    ZEROEQHTTP_API bool handlePUT( const std::string& endpoint,
                                   servus::Serializable& object );

    /**
     * Subscribe an endpoint to receive HTTP PUT requests.
     *
     * Every receival of the endpoint will call the registered callback function.
     *
     * @param endpoint the endpoint to receive PUT requests for during receive()
     * @param func the callback function for serving the PUT request
     * @return true if subscription was successful, false otherwise
     */
    ZEROEQHTTP_API
    bool handlePUT( const std::string& endpoint, const PUTFunc& func );

    /**
     * @overload
     * @param endpoint the endpoint to receive PUT requests for during receive()
     * @param schema describes data layout of endpoint
     * @param func the callback function for serving the PUT request
     */
    ZEROEQHTTP_API bool handlePUT( const std::string& endpoint,
                                   const std::string& schema,
                                   const PUTFunc& func );

    /**
     * Subscribe an endpoint to receive HTTP PUT requests with payload.
     *
     * Every receival of the endpoint will call the registered callback function.
     *
     * @param endpoint the endpoint to receive PUT requests for during receive()
     * @param func the callback function for serving the PUT request
     * @return true if subscription was successful, false otherwise
     */
    ZEROEQHTTP_API bool handlePUT( const std::string& endpoint,
                                   const PUTPayloadFunc& func );

    /**
     * @overload
     * @param endpoint the endpoint to receive PUT requests for during receive()
     * @param schema describes data layout of the endpoint
     * @param func the callback function for serving the PUT request
     */
    ZEROEQHTTP_API bool handlePUT( const std::string& endpoint,
                                   const std::string& schema,
                                   const PUTPayloadFunc& func );
    /**
     * Subscribe a serializable object to serve HTTP GET requests.
     *
     * Every request will be directly handled during receive() by using
     * toJSON(). To track updates on the object, the serializable's received
     * function is called accordingly.
     *
     * The subscribed object instance has to be valid until removeGET().
     *
     * @param object the object to serve during receive()
     * @return true if subscription was successful, false otherwise
     */
    ZEROEQHTTP_API bool handleGET( const servus::Serializable& object );

    /**
     * @overload
     * @param object the object to serve during receive()
     * @param endpoint use this as the URL endpoint instead of the default
     *                 servus::Serializable::getTypeName()
     */
    ZEROEQHTTP_API bool handleGET( const std::string& endpoint,
                                   const servus::Serializable& object );

    /**
     * Subscribe an endpoint to serve HTTP GET requests.
     *
     * Every request will be directly handled during receive() by calling the
     * registered GET function.
     *
     * @param endpoint the endpoint to serve during receive()
     * @param func the callback function for serving the GET request
     * @return true if subscription was successful, false otherwise
     */
    ZEROEQHTTP_API
    bool handleGET( const std::string& endpoint, const GETFunc& func );

    /**
     * @overload
     * @param endpoint the endpoint to serve during receive()
     * @param schema describes data layout of the endpoint
     * @param func the callback function for serving the GET request
     */
    ZEROEQHTTP_API
    bool handleGET( const std::string& endpoint, const std::string& schema,
                    const GETFunc& func );

    /**
     * @return the registered schema for the given object, or empty if not
     *         registered.
     */
    ZEROEQHTTP_API
    std::string getSchema( const servus::Serializable& object ) const;

    /** @overload */
    ZEROEQHTTP_API std::string getSchema( const std::string& endpoint ) const;
    //@}

private:
    class Impl;
    std::unique_ptr< Impl > _impl;

    // Receiver API
    void addSockets( std::vector< detail::Socket >& entries ) final;
    void process( detail::Socket& socket, uint32_t timeout ) final;
    void addConnection( const std::string& ) final { ZEROEQDONTCALL; } // LCOV_EXCL_LINE
};
}
}

#endif
