
/* Copyright (c) 2016-2017, Human Brain Project
 *                          Stefan.Eilemann@epfl.ch
 *                          Daniel.Nachbaur@epfl.ch
 *                          Pawel.Podhajski@epfl.ch
 */

#include "server.h"

#include "requestHandler.h"

#include "../log.h"
#include "../detail/broker.h"
#include "../detail/sender.h"
#include "../detail/socket.h"

#include "jsoncpp/json/json.h"

#include <servus/serializable.h>

#include <algorithm>
#include <array>
#include <future>
#include <thread>

namespace
{
// Transform camelCase to hyphenated-notation, e.g.
// lexis/render/LookOut -> lexis/render/look-out
// Inspiration: https://gist.github.com/rodamber/2558e25d4d8f6b9f2ffdf7bd49471340
std::string _camelCaseToHyphenated( std::string camelCase )
{
    if( camelCase.empty( ))
        return camelCase;

    std::string str( 1, tolower(camelCase[0]) );
    for( auto it = camelCase.begin() + 1; it != camelCase.end(); ++it )
    {
        if( isupper(*it) && *(it-1) != '-' && islower(*(it-1)))
            str += "-";
        str += *it;
    }

    std::transform( str.begin(), str.end(), str.begin(), ::tolower );
    return str;
}

// http://stackoverflow.com/questions/5343190
std::string _replaceAll( std::string subject, const std::string& search,
                         const std::string& replace )
{
    size_t pos = 0;
    while( (pos = subject.find( search, pos )) != std::string::npos )
    {
        subject.replace( pos, search.length(), replace );
        pos += replace.length();
    }
    return subject;
}

// convert name to lowercase with '/' separators instead of '::'
void _convertEndpointName( std::string& endpoint )
{
    endpoint = _camelCaseToHyphenated( _replaceAll( endpoint, "::", "/" ));
}

const std::string REQUEST_REGISTRY = "registry";
const std::string REQUEST_SCHEMA = "schema";

bool _endsWithSchema( const std::string& uri )
{
    if( uri.length() < REQUEST_SCHEMA.length( ))
        return false;
    return uri.compare( uri.length() - REQUEST_SCHEMA.length(),
                        std::string::npos, REQUEST_SCHEMA ) == 0;
}
void _checkAndConvertEndpointName( std::string& endpoint )
{
    if( endpoint.empty( ))
        ZEROEQTHROW( std::runtime_error( "endpoint name cannot be empty" ));

    _convertEndpointName( endpoint );
    if( endpoint == REQUEST_REGISTRY )
        ZEROEQTHROW( std::runtime_error(
                         "'registry' not allowed as endpoint name" ));
}

} // unnamed namespace


namespace zeroeq
{
namespace http
{

class Server::Impl : public detail::Sender
{
public:
    Impl() : Impl( URI( )) {}

    Impl( const URI& uri_ )
        : detail::Sender( URI( _getInprocURI( )), 0, ZMQ_PAIR )
        , _requestHandler( _getInprocURI(), getContext( ))
        , _httpOptions( _requestHandler )
        , _httpServer( _httpOptions.
                       // INADDR_ANY translation: zmq -> boost.asio
                       address( uri_.getHost() == "*" ? "0.0.0.0"
                                                      : uri_.getHost( )).
                       port( std::to_string( int(uri_.getPort( )))).
                       protocol_family( HTTPServer::options::ipv4 ).
                       reuse_address( true ) )
    {
        if( ::zmq_bind( socket, _getInprocURI().c_str( )) == -1 )
        {
            ZEROEQTHROW( std::runtime_error(
                             "Cannot bind HTTPServer to inproc socket" ));
        }

        try
        {
            _httpServer.listen();
            _httpThread.reset( new std::thread( [&]
            {
                try
                {
                    _httpServer.run();
                }
                catch( const std::exception& e )
                {
                    ZEROEQERROR << "Error during HTTPServer::run(): "
                                << e.what() << std::endl;
                }
            }));
        }
        catch( const std::exception& e )
        {
            ZEROEQTHROW( std::runtime_error(
                             std::string( "Error while starting HTTP server: " )
                             + e.what( )));
        }

        uri = URI();
        uri.setHost( _httpServer.address( ));
        uri.setPort( std::stoi( _httpServer.port( )));
    }

    ~Impl()
    {
        if( _httpThread )
        {
            _httpServer.stop();
            _httpThread->join();
        }
    }

    void _registerSchema( const std::string& endpoint,
                          const std::string& schema )
    {
        const std::string exist = _returnSchema( endpoint );
        if( exist.empty( ))
            _schemas[ endpoint ] = schema;
        else if( schema != exist )
            ZEROEQTHROW( std::runtime_error(
                             "Schema registered for endpoint differs: "
                             + endpoint  ));
    }

    bool remove( const servus::Serializable& serializable )
    {
        return remove( serializable.getTypeName( ));
    }

    bool remove( std::string endpoint )
    {
        _convertEndpointName( endpoint );
        _schemas.erase( endpoint );
        const bool foundPUT = _methods[int(Verb::PUT)].erase( endpoint ) != 0;
        const bool foundGET = _methods[int(Verb::GET)].erase( endpoint ) != 0;
        return foundPUT || foundGET;
    }

    bool handle( const Verb action, std::string endpoint, RESTFunc func )
    {
        _checkAndConvertEndpointName( endpoint );

        if( _methods[int(action)].count( endpoint ) != 0 )
            return false;

        _methods[int(action)][ endpoint ] = func;
        return true;
    }

    bool handlePath( const Verb action, std::string endpoint,
                     RESTPathFunc func )
    {
        _checkAndConvertEndpointName( endpoint );

        if( _pathMethods[int(action)].count( endpoint ) != 0 )
            return false;

        _pathMethods[int(action)][ endpoint ] = func;
        return true;
    }

    bool handlePUT( const std::string& endpoint,
                    servus::Serializable& serializable )
    {
        const auto func = [&serializable]( const std::string& json )
        { return serializable.fromJSON( json ); };
        return handlePUT( endpoint, serializable.getSchema(), func );
    }

    bool handlePUT( std::string endpoint, const std::string& schema,
                    const PUTPayloadFunc& func )
    {
        _checkAndConvertEndpointName( endpoint );
        if( _methods[int(Verb::PUT)].count( endpoint ) != 0 )
            return false;


        RESTFunc futureFunc = [ func ]( const std::string& param )
        {
            const auto code = func( param ) ? Code::OK : Code::BAD_REQUEST;
            return make_ready_future( Response{ code } );
        };
        _methods[int(Verb::PUT)][ endpoint ] = futureFunc;
        _registerSchema( endpoint, schema );

        return true;
    }

    bool handleGET( const std::string& endpoint,
                    const servus::Serializable& serializable )
    {
        const auto func = [&serializable] { return serializable.toJSON(); };
        return handleGET( endpoint, serializable.getSchema(), func );
    }

    bool handleGET( std::string endpoint, const std::string& schema,
                    const GETFunc& func )
    {
        _checkAndConvertEndpointName( endpoint );

        if( _methods[int(Verb::GET)].count( endpoint ) != 0 )
            return false;

        RESTFunc futureFunc = [ func ]( const std::string& )
        {
            return make_ready_future( Response{ Code::OK, func() } );
        };
        _methods[int(Verb::GET)][ endpoint ] = futureFunc;

        if( !schema.empty( ))
            _registerSchema( endpoint, schema );
        return true;
    }

    std::string getSchema( std::string endpoint ) const
    {
        _convertEndpointName( endpoint );
        return _returnSchema( endpoint );
    }

    void addSockets( std::vector< detail::Socket >& entries )
    {
        detail::Socket entry;
        entry.socket = socket;
        entry.events = ZMQ_POLLIN;
        entries.push_back( entry );
    }

    void process()
    {
        HTTPRequest* request = nullptr;
        ::zmq_recv( socket, &request, sizeof( request ), 0 );
        if( !request )
            ZEROEQTHROW( std::runtime_error(
                             "Could not receive HTTP request from HTTP server" ));

        switch( request->method )
        {
        case HTTPRequest::Method::GET:
            _processRequest( *request, Verb::GET );
            break;
        case HTTPRequest::Method::PUT:
            _processRequest( *request, Verb::PUT );
            break;
        case HTTPRequest::Method::POST:
            _processRequest( *request, Verb::POST );
            break;
        case HTTPRequest::Method::PATCH:
            _processRequest( *request, Verb::PATCH );
            break;
        case HTTPRequest::Method::DELETE:
            _processRequest( *request, Verb::DELETE );
            break;
        default:
            ZEROEQTHROW( std::runtime_error(
                             "Encountered invalid HTTP method to process: " +
                             std::to_string( int( request->method ))));
        }

        bool done = true;
        ::zmq_send( socket, &done, sizeof( done ), 0 );
    }

protected:
    // key stores endpoints lower-case, hyphenated with '/' separators
    typedef std::map< std::string, RESTFunc > FuncMap;
    typedef std::map< std::string, RESTPathFunc, std::greater< std::string >>
                                                               PathFuncMap;
    typedef std::map< std::string, std::string > SchemaMap;

    SchemaMap _schemas;
    std::array<FuncMap, 5> _methods;
    std::array<PathFuncMap, 5> _pathMethods;

    RequestHandler _requestHandler;
    HTTPServer::options _httpOptions;
    HTTPServer _httpServer;
    std::unique_ptr< std::thread > _httpThread;

    std::string _getInprocURI() const
    {
        std::ostringstream inprocURI;
        // No socket notifier possible on inproc ZMQ sockets,
        // (https://github.com/zeromq/libzmq/issues/1434).
        // Use inproc on Windows as ipc is not supported there, which means
        // we do not support notifications on Windows...
#ifdef _MSC_VER
        inprocURI << "inproc://#" << static_cast< const void* >( this );
#else
        inprocURI << "ipc:///tmp/" << static_cast< const void* >( this );
#endif
        return inprocURI.str();
    }

    std::string _getEndpoint( const std::string& url )
    {
        if( url.size() < 2 )
            return url;

        return _camelCaseToHyphenated( url.substr( 1 ));
    }

    std::string _returnRegistry() const
    {
        Json::Value body( Json::objectValue );
        for( const auto& i : _methods[int(Verb::GET)] )
            body[i.first].append( "GET" );
        for( const auto& i : _methods[int(Verb::PUT)] )
            body[i.first].append( "PUT" );
        for( const auto& i : _methods[int(Verb::POST)] )
            body[i.first].append( "POST" );
        for( const auto& i : _methods[int(Verb::DELETE)] )
            body[i.first].append( "DELETE" );
        for( const auto& i : _methods[int(Verb::PATCH)] )
            body[i.first].append( "PATCH" );
        return body.toStyledString();
    }

    std::string _returnSchema( const std::string& endpoint ) const
    {
        const auto& i = _schemas.find( endpoint );
        return i != _schemas.end() ? i->second : std::string();
    }

    Response _getSchemaResponse( const std::string& endpoint ) const
    {
        const auto it = _schemas.find( endpoint );
        if( it == _schemas.end( ))
            return Response{ Code::NOT_FOUND };
        return Response{ Code::OK, it->second };
    }

    void _processRequest( HTTPRequest& request, Verb verb )
    {
        const auto& funcMap = _methods[int(verb)];
        const auto& pathFuncMap = _pathMethods[int(verb)];

        const std::string& endpoint = _getEndpoint( request.url );
        const auto kv = funcMap.find( endpoint );
        if( kv != funcMap.end( ))
        {
            request.response = kv->second( request.request );
            return;
        }

        const auto it = std::find_if( pathFuncMap.begin(), pathFuncMap.end(),
        [&endpoint]( const std::pair<std::string, RESTPathFunc>& pair )
        {

            const auto& prefix = pair.first;
            if( prefix == endpoint )
                return true;
            return endpoint.find( prefix + "/" ) == 0;
        });

        if( it != pathFuncMap.end( ))
        {
            const auto& prefix = it->first;
            if( prefix == endpoint )
            {
                request.response = it->second( "", request.request );
                return;
            }
            const auto path = endpoint.substr( prefix.size() + 1,
                                               endpoint.size( ));
            request.response = it->second( path, request.request );
            return;
        }

        if( verb == Verb::GET)
        {
            if( endpoint == REQUEST_REGISTRY )
            {
                Response response{ Code::OK, _returnRegistry() };
                request.response = make_ready_future( response );
                return;
            }

            if( _endsWithSchema( endpoint ))
            {
                const auto key = endpoint.substr( 0, endpoint.find_last_of( '/' ));
                request.response = make_ready_future( _getSchemaResponse( key ));
                return;
            }
        }
        request.response = make_ready_future( Response{ Code::NOT_FOUND } );
    }
};

namespace
{
std::string _getServerParameter( const int argc, const char* const* argv )
{
    for( int i = 0; i < argc; ++i  )
    {
        if( std::string( argv[i] ) == "--zeroeq-http-server" )
        {
            if( i == argc - 1 || argv[ i + 1 ][0] == '-' )
                return "tcp://";
            return argv[i+1];
        }
    }
    return std::string();
}
}

Server::Server( const URI& uri, Receiver& shared )
    : Receiver( shared )
    , _impl( new Impl( uri ))
{}

Server::Server( const URI& uri )
    : Receiver()
    , _impl( new Impl( uri ))
{}

Server::Server( Receiver& shared )
    : Receiver( shared )
    , _impl( new Impl )
{}

Server::Server()
    : Receiver()
    , _impl( new Impl )
{}

Server::~Server()
{}

std::unique_ptr< Server > Server::parse( const int argc,
                                         const char* const* argv )
{
    const std::string& param = _getServerParameter( argc, argv );
    if( param.empty( ))
        return nullptr;

    return std::unique_ptr< Server >( new Server( URI( param )));
}

std::unique_ptr< Server > Server::parse( const int argc,
                                         const char* const* argv,
                                         Receiver& shared )
{
    const std::string& param = _getServerParameter( argc, argv );
    if( param.empty( ))
        return nullptr;

    return std::unique_ptr< Server >( new Server( URI( param ), shared ));
}

const URI& Server::getURI() const
{
    return _impl->uri;
}

SocketDescriptor Server::getSocketDescriptor() const
{
#ifdef _MSC_VER
    ZEROEQTHROW( std::runtime_error(
                 std::string( "HTTP server socket descriptor not available" )));
#else
    SocketDescriptor fd = 0;
    size_t fdLength = sizeof(fd);
    if( ::zmq_getsockopt( _impl->socket, ZMQ_FD, &fd, &fdLength ) == -1 )
    {
        ZEROEQTHROW( std::runtime_error(
                         std::string( "Could not get socket descriptor" )));
    }
    return fd;
#endif
}

bool Server::handle( const std::string& endpoint, servus::Serializable& object )
{
    return handlePUT( endpoint, object ) && handleGET( endpoint, object );
}

bool Server::handle( const Verb action, const std::string& endpoint,
                     RESTFunc func )
{
    return _impl->handle( action,  endpoint, func );
}

bool Server::handlePath( const Verb action, const std::string& endpoint,
                         RESTPathFunc func )
{
    return _impl->handlePath( action, endpoint, func );
}

bool Server::remove( const servus::Serializable& object )
{
    return _impl->remove( object );
}

bool Server::remove( const std::string& endpoint )
{
    return _impl->remove( endpoint );
}

bool Server::handlePUT( servus::Serializable& object )
{
    return _impl->handlePUT( object.getTypeName(), object );
}

bool Server::handlePUT( const std::string& endpoint,
                        servus::Serializable& object )
{
    return _impl->handlePUT( endpoint, object );
}

bool Server::handlePUT( const std::string& endpoint, const PUTFunc& func )
{
    return _impl->handlePUT( endpoint, "",
                             [func]( const std::string& ) { return func(); } );
}

bool Server::handlePUT( const std::string& endpoint, const std::string& schema,
                        const PUTFunc& func )
{
    return _impl->handlePUT( endpoint, schema,
                             [func]( const std::string& ) { return func(); } );
}

bool Server::handlePUT( const std::string& endpoint,
                        const PUTPayloadFunc& func )
{
    return _impl->handlePUT( endpoint, "", func );
}

bool Server::handlePUT( const std::string& endpoint,const std::string& schema,
                        const PUTPayloadFunc& func )
{
    return _impl->handlePUT( endpoint, schema, func );
}

bool Server::handleGET( const servus::Serializable& object )
{
    return _impl->handleGET( object.getTypeName(), object );
}

bool Server::handleGET( const std::string& endpoint,
                        const servus::Serializable& object )
{
    return _impl->handleGET( endpoint, object );
}

bool Server::handleGET( const std::string& endpoint, const GETFunc& func )
{
    return _impl->handleGET( endpoint, "", func );
}

bool Server::handleGET( const std::string& endpoint, const std::string& schema,
                        const GETFunc& func )
{
    return _impl->handleGET( endpoint, schema, func );
}

std::string Server::getSchema( const servus::Serializable& object ) const
{
    return _impl->getSchema( object.getTypeName( ));
}

std::string Server::getSchema( const std::string& endpoint ) const
{
    return _impl->getSchema( endpoint );
}

void Server::addSockets( std::vector< detail::Socket >& entries )
{
    _impl->addSockets( entries );
}

void Server::process( detail::Socket&, const uint32_t )
{
    _impl->process();
}

}
}
