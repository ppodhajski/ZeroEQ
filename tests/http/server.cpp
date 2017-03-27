
/* Copyright (c) 2016-2017, Human Brain Project
 *                          Stefan.Eilemann@epfl.ch
 *                          Daniel.Nachbaur@epfl.ch
 *                          Pawel.Podhajski@epfl.ch
 */

#define BOOST_TEST_MODULE http_server

#include <zeroeq/http/helpers.h>
#include <zeroeq/http/request.h>
#include <zeroeq/http/response.h>
#include <zeroeq/http/server.h>
#include <zeroeq/subscriber.h>
#include <zeroeq/uri.h>
#include <servus/serializable.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <thread>

#include <boost/network/protocol/http/client.hpp>
#include <boost/network/protocol/http/server.hpp>

namespace
{
namespace http = boost::network::http;

// default client from cppnetlib is asynchronous, not handy for tests
using HTTPClient =
    http::basic_client< http::tags::http_default_8bit_tcp_resolve, 1, 1 >;

using ServerReponse = http::basic_response<http::tags::http_server>;

/** @return ready future wrapping the value passed. */
template<typename T>
std::future<T> make_ready_future( const T value )
{
    std::promise<T> promise;
    promise.set_value( value );
    return promise.get_future();
}

struct Response
{
    const ServerReponse::status_type status;
    const std::string body;
    std::map< const std::string, const std::string > additionalHeaders;

    Response( const ServerReponse::status_type status_,
              const std::string& body_ )
        : status{ status_ }, body{ body_ }{}

    Response( const ServerReponse::status_type status_,
              const std::string& body_,
              const std::map< const std::string, const std::string > map  )
        : status{ status_ }, body{ body_ }
        , additionalHeaders{ map }{}
};

Response _buildResponse( const std::string& body = std::string( ))
{
    return { ServerReponse::ok, body };
}

const Response response200{ ServerReponse::ok, "" };
const Response error400{ ServerReponse::bad_request, "" };
const Response error404{ ServerReponse::not_found, "" };
const Response error405{ ServerReponse::method_not_allowed, "" };

const std::string jsonGet( "Not JSON, just want to see that the is data a-ok" );
const std::string jsonPut( "See what my stepbrother jsonGet says" );

class Foo : public servus::Serializable
{
public:
    Foo() { _notified = false; }

    void setNotified( const bool notified = true ) { _notified = notified; }
    bool getNotified() const { return _notified; }

    std::string getSchema() const final
    {
        return "{\n  '_notified' : 'bool'\n}";
    }

private:
    std::string getTypeName() const final { return "test::Foo"; }
    virtual zeroeq::uint128_t getTypeIdentifier() const final
        { return servus::make_uint128( getTypeName( )); }

    bool _fromJSON( const std::string& json ) final
    {
        return jsonPut == json;
    }

    std::string _toJSON() const final
    {
        return jsonGet;
    }

    bool _notified;
};


class Client : public HTTPClient
{
public:
    Client( const zeroeq::URI& uri )
    {
        std::ostringstream url;
        url << "http://" << uri.getHost() << ":" << uri.getPort();
        _baseURL = url.str();
    }

    ~Client()
    {
    }

    void check( zeroeq::http::Method method, const std::string& request,
                const std::string& data, const Response& expected,
                const int line )
    {
        _checkImpl( method, request, data, expected, line );
    }

    void checkGET( const std::string& request,
                   const Response& expected, const int line )
    {
        _checkImpl( zeroeq::http::Method::GET, request, "", expected, line );
    }

    void checkPUT( const std::string& request, const std::string& data,
                   const Response& expected, const int line )
    {
        _checkImpl( zeroeq::http::Method::PUT, request, data, expected, line );
    }

    void checkPOST( const std::string& request, const std::string& data,
                    const Response& expected, const int line )
    {
        _checkImpl( zeroeq::http::Method::POST, request, data, expected, line );
    }

    void sendGET( const std::string& request )
    {
        HTTPClient::request request_( _baseURL + request );
        get( request_ );
    }

private:
    std::string _baseURL;

    response patch( request request, string_type const& body = string_type(),
            string_type const& content_type = string_type(),
            body_callback_function_type body_handler =
            body_callback_function_type(),
            body_generator_function_type body_generator =
            body_generator_function_type( ))
    {
        namespace bn = boost::network;
        if (body != string_type())
        {
            request << bn::remove_header("Content-Length")
                    << bn::header("Content-Length",
                                  std::to_string(body.size( )))
                    << bn::body(body);
        }
        typename bn::http::headers_range<basic_client_facade::request >::type
            content_type_headers = bn::http::headers( request )["Content-Type"];
        if (content_type != string_type( ))
        {
            if (!boost::empty( content_type_headers ))
                request << bn::remove_header( "Content-Type" );
            request << bn::header( "Content-Type", content_type );
        } else
        {
            if (boost::empty( content_type_headers ))
            {
                typedef typename bn::char_
                < http::tags::http_default_8bit_tcp_resolve >::type char_type;
                static char_type content_arr[] = "x-application/octet-stream";
                request << bn::header( "Content-Type", content_arr );
            }
        }
        return pimpl->request_skeleton(request, "PATCH", true, body_handler,
                                       body_generator);
    }

    void _checkImpl( const zeroeq::http::Method method,
                     const std::string& request,
                     const std::string& data,
                     const Response& expected, const int line )
    {
        HTTPClient::request request_( _baseURL + request );
        HTTPClient::response response;

        switch( method )
        {
        case zeroeq::http::Method::GET:
            response = get( request_ );
            break;
        case zeroeq::http::Method::POST:
            response = post( request_, data );
            break;
        case zeroeq::http::Method::PUT:
            response = put( request_, data );
            break;
        case zeroeq::http::Method::DELETE:
            response = delete_( request_ );
        case zeroeq::http::Method::PATCH:
            response = patch( request_, data  );
            break;
        default:
            throw std::runtime_error("Missing method in test");
        }

        BOOST_CHECK_MESSAGE( status( response ) == expected.status,
                             "At l." + std::to_string( line ) + ": " +
                             std::to_string( status( response ) )+ " != " +
                             std::to_string( int(expected.status) )  );

        std::map< std::string, std::string > expectedHeaders;
        expectedHeaders.insert( { "Access-Control-Allow-Headers",
                                  "Content-Type" });
        expectedHeaders.insert( { "Access-Control-Allow-Methods",
                                  "GET,PUT,POST,PATCH,DELETE,OPTIONS" });
        expectedHeaders.insert( { "Access-Control-Allow-Origin", "*" });

        expectedHeaders.insert( std::make_pair( "Content-Length",
                                std::to_string( expected.body.size( ))));

        for ( const auto& header : expected.additionalHeaders )
            expectedHeaders.insert( header );

        const auto& responseHeaders = headers( response );
        const size_t responseHeaderSize =
                std::distance( responseHeaders.begin(), responseHeaders.end( ));
        BOOST_REQUIRE_MESSAGE( responseHeaderSize == expectedHeaders.size(),
                               "At l." + std::to_string( line ) + ": " +
                               std::to_string( responseHeaderSize )+ " != " +
                               std::to_string( expectedHeaders.size( )));
        auto i = responseHeaders.begin();
        auto j = expectedHeaders.begin();

        for( ; i != responseHeaders.end() && j != expectedHeaders.end(); ++i, ++j )
        {
            BOOST_CHECK_EQUAL( i->first, j->first );
            BOOST_CHECK_EQUAL( i->second, j->second );
        }

        auto responseBody = static_cast< std::string >( body( response ));
        std::cout << responseBody << std::endl;
        // weird stuff here: cppnetlib client response body string is
        // duplicated, hence take first half for comparison
        responseBody.erase( responseBody.size() / 2, responseBody.size() / 2 );
        BOOST_CHECK_MESSAGE( responseBody == expected.body,
                             "At l." + std::to_string( line ) + ": " +
                             responseBody + " != " + expected.body );
    }
};

}

BOOST_AUTO_TEST_CASE(construction)
{
    zeroeq::http::Server server1;
    BOOST_CHECK_NE( server1.getURI().getHost(), "" );
    BOOST_CHECK_NE( server1.getURI().getHost(), "*" );
    BOOST_CHECK_NE( server1.getURI().getPort(), 0 );
    BOOST_CHECK_NO_THROW( server1.getSocketDescriptor( ));
    BOOST_CHECK_GT( server1.getSocketDescriptor(), 0 );

    const zeroeq::URI uri( "tcp://" );
    zeroeq::http::Server server2( uri );
    zeroeq::http::Server server3( uri );
    BOOST_CHECK_NE( server2.getURI(), server3.getURI( ));
    BOOST_CHECK_NE( server2.getURI().getPort(), 0 );

    BOOST_CHECK_THROW( zeroeq::http::Server( server2.getURI( )),
                       std::runtime_error );
    BOOST_CHECK_NO_THROW( server2.getSocketDescriptor( ));
    BOOST_CHECK_GT( server1.getSocketDescriptor(), 0 );
}

BOOST_AUTO_TEST_CASE(construction_argv_host_port)
{
    const char* app = boost::unit_test::framework::master_test_suite().argv[0];
    const char* argv[] = { app, "--zeroeq-http-server", "127.0.0.1:0" };
    const int argc = sizeof(argv)/sizeof(char*);

    std::unique_ptr< zeroeq::http::Server > server1 =
            zeroeq::http::Server::parse( argc, argv );

    BOOST_REQUIRE( server1 );
    BOOST_CHECK_EQUAL( server1->getURI().getHost(), "127.0.0.1" );
    BOOST_CHECK_NE( server1->getURI().getPort(), 0 );

    zeroeq::Subscriber shared;
    std::unique_ptr< zeroeq::http::Server > server2 =
            zeroeq::http::Server::parse( argc, argv, shared );

    BOOST_REQUIRE( server2 );
    BOOST_CHECK_EQUAL( server2->getURI().getHost(), "127.0.0.1" );
    BOOST_CHECK_NE( server2->getURI().getPort(), 0 );
}

BOOST_AUTO_TEST_CASE(construction_argv)
{
    const char* app = boost::unit_test::framework::master_test_suite().argv[0];
    const char* argv[] = { app, "--zeroeq-http-server" };
    const int argc = sizeof(argv)/sizeof(char*);

    std::unique_ptr< zeroeq::http::Server > server1 =
            zeroeq::http::Server::parse( argc, argv );

    BOOST_REQUIRE( server1 );
    BOOST_CHECK( !server1->getURI().getHost().empty( ));
    BOOST_CHECK_NE( server1->getURI().getPort(), 0 );

    zeroeq::Subscriber shared;
    std::unique_ptr< zeroeq::http::Server > server2 =
            zeroeq::http::Server::parse( argc, argv, shared );

    BOOST_CHECK( !server2->getURI().getHost().empty( ));
    BOOST_CHECK_NE( server2->getURI().getPort(), 0 );
}

BOOST_AUTO_TEST_CASE(construction_empty_argv)
{
    const char* app = boost::unit_test::framework::master_test_suite().argv[0];
    const char* argv[] = { app };
    const int argc = sizeof(argv)/sizeof(char*);

    std::unique_ptr< zeroeq::http::Server > server1 =
            zeroeq::http::Server::parse( argc, argv );

    BOOST_CHECK( !server1 );

    zeroeq::Subscriber shared;
    std::unique_ptr< zeroeq::http::Server > server2 =
            zeroeq::http::Server::parse( argc, argv, shared );

    BOOST_CHECK( !server2 );
}

BOOST_AUTO_TEST_CASE(registration)
{
    zeroeq::http::Server server;
    Foo foo;
    BOOST_CHECK( server.handleGET( foo ));
    BOOST_CHECK( !server.handleGET( foo ));
    BOOST_CHECK( server.remove( foo ));
    BOOST_CHECK( !server.remove( foo ));

    BOOST_CHECK( server.handleGET( "foo", [](){ return "bla"; } ));
    BOOST_CHECK( !server.handleGET( "foo", [](){ return "bla"; } ));
    BOOST_CHECK( server.remove( "foo" ));
    BOOST_CHECK( !server.remove( "foo" ));

    BOOST_CHECK( server.handleGET( "bar", "schema", [](){ return "bla"; } ));
    BOOST_CHECK( !server.handleGET( "bar", "schema", [](){ return "bla"; } ));
    BOOST_CHECK( server.remove( "bar" ));
    BOOST_CHECK( !server.remove( "bar" ));

    BOOST_CHECK( server.handlePUT( foo ));
    BOOST_CHECK( !server.handlePUT( foo ));
    BOOST_CHECK( server.remove( foo ));
    BOOST_CHECK( !server.remove( foo ));

    BOOST_CHECK( server.handlePUT( "foo",
                                  []( const std::string& ) { return true; }));
    BOOST_CHECK( !server.handlePUT( "foo",
                                  []( const std::string& ) { return true; }));
    BOOST_CHECK( server.remove( "foo" ));
    BOOST_CHECK( !server.remove( "foo" ));

    BOOST_CHECK( server.handlePUT( "bar", "schema",
                                  []( const std::string& ) { return true; }));
    BOOST_CHECK( !server.handlePUT( "bar", "schema",
                                  []( const std::string& ) { return true; }));
    BOOST_CHECK_EQUAL( server.getSchema( "bar" ), "schema" );
    BOOST_CHECK( server.handleGET( "bar", "schema", [](){ return "bla"; } ));
    BOOST_CHECK( !server.handleGET( "bar", "schema", [](){ return "bla"; } ));
    BOOST_CHECK_EQUAL( server.getSchema( "bar" ), "schema" );
    BOOST_CHECK( server.remove( "bar" ));
    BOOST_CHECK( !server.remove( "bar" ));
    BOOST_CHECK_EQUAL( server.getSchema( "bar" ), "" );

    BOOST_CHECK( server.handle( foo ));
    BOOST_CHECK( !server.handle( foo ));
    BOOST_CHECK( server.remove( foo ));
    BOOST_CHECK( !server.remove( foo ));

    BOOST_CHECK( server.handle( foo ));
    BOOST_CHECK_EQUAL( server.getSchema( foo ), foo.getSchema( ));
    BOOST_CHECK( server.remove( foo ));
    BOOST_CHECK_EQUAL( server.getSchema( foo ), std::string( ));
}

BOOST_AUTO_TEST_CASE(get_serializable)
{
    bool running = true;
    zeroeq::http::Server server;
    Foo foo;

    foo.registerSerializeCallback( [&]{ foo.setNotified(); });
    server.handleGET( foo );

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));

    client.checkGET( "/unknown", error404, __LINE__ );
    BOOST_CHECK( !foo.getNotified( ));

    client.checkGET( "/test/foo", _buildResponse( jsonGet ), __LINE__ );
    BOOST_CHECK( foo.getNotified( ));

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(get_event)
{
    bool running = true;
    zeroeq::http::Server server;

    bool requested = false;
    server.handleGET( "test/foo", [&](){ requested = true; return jsonGet; } );

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));

    client.checkGET( "/unknown", error404, __LINE__ );
    BOOST_CHECK( !requested );

    // regression check for bugfix #190 (segfault with GET '/')
    client.checkGET( "/", error404, __LINE__ );
    BOOST_CHECK( !requested );

    client.checkGET( "/test/foo", _buildResponse( jsonGet ), __LINE__ );
    BOOST_CHECK( requested );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(shared)
{
    bool running = true;
    zeroeq::Subscriber subscriber;
    zeroeq::http::Server server1( subscriber );
    zeroeq::http::Server server2( server1 );
    Foo foo;
    server2.handleGET( foo );

    std::thread thread( [&]() { while( running ) subscriber.receive( 100 );});

    Client client1( server1.getURI( ));
    Client client2( server2.getURI( ));

    client1.checkGET( "/test/foo", error404, __LINE__ );
    client2.checkGET( "/test/foo", _buildResponse( jsonGet ), __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(put_serializable)
{
    bool running = true;
    zeroeq::http::Server server;
    Foo foo;

    foo.registerDeserializedCallback( [&]{ foo.setNotified(); });
    server.handlePUT( foo );

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkPUT( "/test/foo", "", error400, __LINE__ );
    client.checkPUT( "/test/foo", "Foo", error400, __LINE__ );
    client.checkPUT( "/test/bar", jsonPut, error404, __LINE__ );
    BOOST_CHECK( !foo.getNotified( ));

    client.checkPUT( "/test/foo", jsonPut, response200, __LINE__ );
    BOOST_CHECK( foo.getNotified( ));

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(put_event)
{
    bool running = true;
    zeroeq::http::Server server;

    bool receivedEmpty = false;
    server.handlePUT( "empty", [&]() { receivedEmpty = true; return true; });
    server.handlePUT( "foo", [&]( const std::string& received )
                             { return jsonPut == received; });

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkPUT( "/foo", "", error400, __LINE__ );
    client.checkPUT( "/foo", "Foo", error400, __LINE__ );
    client.checkPUT( "/test/bar", jsonPut, error404, __LINE__ );
    client.checkPUT( "/foo", jsonPut, response200, __LINE__ );
    client.checkPUT( "/empty", " ", response200, __LINE__ );
    BOOST_CHECK( receivedEmpty );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(post)
{
    bool running = true;
    zeroeq::http::Server server;
    server.handle( zeroeq::http::Method::POST, "test",
    [this]( const zeroeq::http::Request& )
    {
        zeroeq::http::Response response{ zeroeq::http::Code::OK };
        return zeroeq::http::make_ready_future( response );
    });

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkPOST( "/test", jsonPut, response200, __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(handle_all)
{
    bool running = true;
    zeroeq::http::Server server;

    for ( int method = int( zeroeq::http::Method::GET );
          method != int( zeroeq::http::Method::DELETE ); ++method )
    {
        server.handle( zeroeq::http::Method( method ), "test",
	               [this]( const zeroeq::http::Request& request )
        {
            zeroeq::http::Response response{ zeroeq::http::Code::OK };
            response.body = request.body;
            return zeroeq::http::make_ready_future( response );
        });
    }
    server.handle( zeroeq::http::Method::GET, "missing",
                   [this]( const zeroeq::http::Request& )
    {
        zeroeq::http::Response response{ zeroeq::http::Code::NO_CONTENT };
        return zeroeq::http::make_ready_future( response );
    });

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });
    Client client( server.getURI( ));

    const Response expectedResponse{ ServerReponse::ok, "bar"};
    for ( int method = int( zeroeq::http::Method::POST );
          method != int( zeroeq::http::Method::DELETE ); ++method )
    {
        client.check( zeroeq::http::Method( method ), "/test", "bar",
                      expectedResponse, __LINE__ );
    }

    const Response expectedResponseGET{ ServerReponse::ok, ""  };
    client.check( zeroeq::http::Method::GET, "/test", "",
                  expectedResponseGET, __LINE__  );

    const Response expectedResponseNegative{ ServerReponse::no_content, ""  };
    client.check( zeroeq::http::Method::GET, "/missing", "",
                  expectedResponseNegative, __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(handle_root)
{
    bool running = true;
    zeroeq::http::Server server;

    server.handle( zeroeq::http::Method::GET, "/",
                   [this]( const zeroeq::http::Request& )
    {
        zeroeq::http::Response response{ zeroeq::http::Code::OK };
        response.body = "homepage";
        return zeroeq::http::make_ready_future( response );
    });

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });
    Client client( server.getURI( ));

    const Response expectedResponse{ ServerReponse::ok, "homepage"};
    client.check( zeroeq::http::Method::GET, "/", "", expectedResponse,
                  __LINE__  );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(handle_path)
{
    bool running = true;
    zeroeq::http::Server server;

    for ( int method = int( zeroeq::http::Method::GET );
          method != int( zeroeq::http::Method::DELETE ); ++method )
    {
        server.handle( zeroeq::http::Method( method ), "test/",
                       [this]( const zeroeq::http::Request& request )
        {
            zeroeq::http::Response response{ zeroeq::http::Code::OK };
            response.body = request.body + "_" + request.path;
            return zeroeq::http::make_ready_future( response );
        });
    }

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });
    Client client( server.getURI( ));

    const Response expectedResponse{ ServerReponse::ok, "payload_path/suffix" };
    for ( int method = int( zeroeq::http::Method::POST );
          method != int( zeroeq::http::Method::DELETE ); ++method )
    {
        client.check( zeroeq::http::Method( method ), "/test/path/suffix",
                      "payload", expectedResponse, __LINE__ );
    }
    client.check( zeroeq::http::Method::GET, "/test/path/suffix", "",
                  Response { ServerReponse::ok, "_path/suffix" }, __LINE__ );

    server.handle( zeroeq::http::Method::GET , "api/object/property/",
                   [this]( const zeroeq::http::Request& request )
    {
        zeroeq::http::Response response{ zeroeq::http::Code::OK };
        response.body = request.path;
        return zeroeq::http::make_ready_future( response );
    });
    client.check( zeroeq::http::Method::GET, "/api/object/property/color", "",
                  Response { ServerReponse::ok, "color" }, __LINE__ );

    server.handle( zeroeq::http::Method::GET , "api/object/property/color/",
                   [this]( const zeroeq::http::Request& request )
    {
        zeroeq::http::Response response{ zeroeq::http::Code::OK };
        response.body = request.path;
        return zeroeq::http::make_ready_future( response );
    });

    client.check( zeroeq::http::Method::GET, "/api/object/property/color/rgb",
                  "", Response { ServerReponse::ok, "rgb" }, __LINE__ );

    server.handle( zeroeq::http::Method::GET , "api/object/size/",
                   [this]( const zeroeq::http::Request& request )
    {
        zeroeq::http::Response response{ zeroeq::http::Code::OK };
        response.body = request.path;
        return zeroeq::http::make_ready_future( response );
    });

    client.check( zeroeq::http::Method::GET, "/api/object/size", "",
                  Response { ServerReponse::not_found, "" }, __LINE__ );

    server.handle( zeroeq::http::Method::GET , "api/object/dimensions/",
                   [this]( const zeroeq::http::Request& request )
    {
        zeroeq::http::Response response{ zeroeq::http::Code::OK };
        response.body = request.path;
        return zeroeq::http::make_ready_future( response );
    });
    client.check( zeroeq::http::Method::GET, "/api/object/dimensions/", "",
                  Response { ServerReponse::ok, "" }, __LINE__ );

    server.handle( zeroeq::http::Method::GET , "api/object/",
                   [this]( const zeroeq::http::Request& request )

    {
        zeroeq::http::Response response{ zeroeq::http::Code::OK };
        response.body = request.path;
        return zeroeq::http::make_ready_future( response );
    });
    client.check( zeroeq::http::Method::GET, "/api/object/", "",
                  Response { ServerReponse::ok, "" }, __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(headers_check)
{
    bool running = true;
    const std::string type = "text/plain";
    const std::string retry = "60";
    const std::string modified = "Wed, 21 Oct 2015 07:00:00 GMT";
    const std::string location = "index.html";

    zeroeq::http::Server server;

    for ( int method = int( zeroeq::http::Method::GET );
          method != int( zeroeq::http::Method::DELETE ); ++method )
    {
        server.handle( zeroeq::http::Method( method ), "test/",
        [ type, retry, modified, location ]( const zeroeq::http::Request&
                                             request)
        {
            zeroeq::http::Response response{ zeroeq::http::Code::OK };
            response.headers[zeroeq::http::Header::CONTENT_TYPE] = type;
            response.headers[zeroeq::http::Header::RETRY_AFTER] = retry;
            response.headers[zeroeq::http::Header::LAST_MODIFIED] = modified;
            response.headers[zeroeq::http::Header::LOCATION] = location;
            response.body = request.body + "_" + request.path;
            return zeroeq::http::make_ready_future( response );
        });
    }

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });
    Client client( server.getURI( ));

    std::map< const std::string, const std::string > headerMap;
    headerMap.insert( { "Content-Type", type });
    headerMap.insert( { "Retry-After", retry });
    headerMap.insert( { "Last-Modified", modified });
    headerMap.insert( { "Location", location });

    const Response expectedResponse{ ServerReponse::ok, "payload_path/suffix",
                                     headerMap };

    for ( int method = int( zeroeq::http::Method::POST );
          method != int( zeroeq::http::Method::DELETE ); ++method )
    {
        client.check( zeroeq::http::Method( method ),
                      "/test/path/suffix", "payload", expectedResponse,
                      __LINE__ );
    }

    const Response expectedResponseGET{ ServerReponse::ok, "_bar/suffix",
                headerMap };
    client.check( zeroeq::http::Method::GET, "/test/bar/suffix", "",
                  expectedResponseGET, __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(largeGet)
{
    bool running = true;
    zeroeq::http::Server server;
    Foo foo;
    server.handleGET( foo );

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    const auto request = std::string( "/test/foo?" ) + std::string( 4096, 'o' );
    client.checkGET( request, _buildResponse( jsonGet ), __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(issue157)
{
    bool running = true;
    zeroeq::http::Server server;
    Foo foo;
    server.handleGET( foo );

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    // Close client before receiving request to provoke #157
    {
        Client client( server.getURI( ));
        const auto request = std::string( "/test/foo?" ) +
                             std::string( 4096, 'o' );
        client.sendGET( request );
    }

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(urlcasesensitivity)
{
    bool running = true;
    zeroeq::http::Server server;
    Foo foo;
    server.handleGET( foo );
    server.handleGET( "BlA/CamelCase", [] { return std::string(); } );

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkGET( "/TEST/FOO", error404, __LINE__ );
    client.checkGET( "/test/foo", _buildResponse( jsonGet ), __LINE__ );

    client.checkGET( "/BlA/CamelCase", response200, __LINE__ );
    client.checkGET( "/bla/camelcase", error404, __LINE__ );
    client.checkGET( "/bla/camel-case", error404, __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(empty_registry)
{
    zeroeq::http::Server server;
    bool running = true;
    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkGET( "/registry", _buildResponse( "{}\n" ), __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(filled_registry)
{
    zeroeq::http::Server server;
    Foo foo;
    server.handle( foo );
    server.handlePUT( "bla/bar", [] { return true; });

    bool running = true;
    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkGET( "/registry",
                     _buildResponse( "{\n   \"bla/bar\" : [ \"PUT\" ],\n"\
                                "   \"test/foo\" : [ \"GET\", \"PUT\" ]\n}\n" ),
                                     __LINE__ );

    client.checkGET( "/bla/bar/registry", error404, __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(object_schema)
{
    zeroeq::http::Server server;
    Foo foo;
    server.handleGET( foo );

    bool running = true;
    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkGET( "/test/foo/schema",
                     _buildResponse( "{\n  '_notified' : 'bool'\n}" ),
                     __LINE__ );
    client.checkGET( "/test/Foo/schema", error404, __LINE__ );
    client.checkGET( "/test/foo/schema/schema", error404, __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(event_schema)
{
    zeroeq::http::Server server;
    const std::string schema = "{ \"value\" : \"boolean\" }";
    server.handleGET( "bla/bar", schema,
                      [] { return std::string( "{ \"value\" : true }"); } );
    server.handlePUT( "bla/FOO", schema,
                      [] { return true; });
    server.handle( zeroeq::http::Method::GET, "bla/boo/",
                   [this]( const zeroeq::http::Request& )
    {
        zeroeq::http::Response response{ zeroeq::http::Code::OK };
        return zeroeq::http::make_ready_future( response );
    });

    bool running = true;
    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkGET( "/bla/bar/schema", _buildResponse( schema ), __LINE__ );
    client.checkGET( "/bla/Bar/schema", error404, __LINE__ );

    client.checkGET( "/bla/FOO/schema", _buildResponse( schema ), __LINE__ );
    client.checkGET( "/bla/foo/schema", error404, __LINE__ );

    client.checkGET( "/bla/boo/", response200, __LINE__ );
    client.checkGET( "/bla/boo/schema", response200, __LINE__ );
    client.checkGET( "/bla/boo/info", response200, __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(event_no_schema)
{
    zeroeq::http::Server server;
    server.handleGET( "bla/bar",
                      [] { return std::string( "{ \"value\" : true }"); });

    bool running = true;
    std::thread thread( [&] { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkGET( "/bla/bar/schema", error404, __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(event_wrong_schema)
{
    zeroeq::http::Server server;
    BOOST_CHECK( server.handlePUT( "bla/foo", "schema",
                                   [] { return true; } ));
    BOOST_CHECK_THROW( server.handleGET( "bla/foo", "bad",
                                         [] { return std::string(); } ),
                       std::runtime_error );

    BOOST_CHECK( server.handleGET( "bar", "schema",
                                   [] { return std::string(); } ));
    BOOST_CHECK_THROW( server.handlePUT( "bar", "bad", [] { return true; } ),
                       std::runtime_error );
}

BOOST_AUTO_TEST_CASE(event_registry_name)
{
    zeroeq::http::Server server;
    BOOST_CHECK_THROW( server.handleGET( "registry",
                                         [] { return std::string(); } ),
                       std::runtime_error );
    BOOST_CHECK_THROW( server.handlePUT( "registry", [] { return true; } ),
                       std::runtime_error );

    BOOST_CHECK_THROW( server.handleGET( "", [] { return std::string(); } ),
                       std::runtime_error );
    BOOST_CHECK_THROW( server.handlePUT( "", [] { return true; } ),
                       std::runtime_error );

    BOOST_CHECK( server.handleGET( "foo/registry",
                                   [] { return std::string( "bar" ); } ));
    BOOST_CHECK( server.handlePUT( "foo/registry", [] { return true; } ));

    bool running = true;
    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkGET( "/foo/registry", _buildResponse( "bar" ), __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(event_schema_name)
{
    zeroeq::http::Server server;
    BOOST_CHECK( server.handleGET( "object", "dummy_schema",
                                   [] { return std::string( "bar" ); } ));

    BOOST_CHECK( server.handlePUT( "object", "dummy_schema",
                                   [] { return true; } ));
    bool running = true;
    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));
    client.checkGET( "/object", _buildResponse( "bar" ), __LINE__ );
    client.checkGET( "/object/schema", _buildResponse( "dummy_schema" ),
                     __LINE__ );

    running = false;
    thread.join();
}

BOOST_AUTO_TEST_CASE(multiple_event_name_for_same_object)
{
    bool running = true;
    zeroeq::http::Server server;
    Foo foo;

    foo.registerSerializeCallback( [&]{ foo.setNotified(); });
    foo.registerDeserializedCallback( [&]{ foo.setNotified(); });

    server.handleGET( foo );
    server.handlePUT( "test/camel-bar", foo );

    std::thread thread( [&]() { while( running ) server.receive( 100 ); });

    Client client( server.getURI( ));

    client.checkPUT( "/test/camel-bar", jsonPut, response200, __LINE__ );
    BOOST_CHECK( foo.getNotified( ));

    client.checkPUT( "/test/foo", "", error404, __LINE__ );

    foo.setNotified( false );

    client.checkGET( "/test/foo", _buildResponse( jsonGet ), __LINE__ );
    BOOST_CHECK( foo.getNotified( ));

    client.checkGET( "/test/camel-bar", error404, __LINE__ );

    running = false;
    thread.join();
}
