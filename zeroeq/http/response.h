/*********************************************************************/
/* Copyright (c) 2017, EPFL/Blue Brain Project                       */
/*                     Pawel Podhajski <pawel.podhajski@epfl.ch>     */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/*   1. Redistributions of source code must retain the above         */
/*      copyright notice, this list of conditions and the following  */
/*      disclaimer.                                                  */
/*                                                                   */
/*   2. Redistributions in binary form must reproduce the above      */
/*      copyright notice, this list of conditions and the following  */
/*      disclaimer in the documentation and/or other materials       */
/*      provided with the distribution.                              */
/*                                                                   */
/*    THIS  SOFTWARE IS PROVIDED  BY THE  UNIVERSITY OF  TEXAS AT    */
/*    AUSTIN  ``AS IS''  AND ANY  EXPRESS OR  IMPLIED WARRANTIES,    */
/*    INCLUDING, BUT  NOT LIMITED  TO, THE IMPLIED  WARRANTIES OF    */
/*    MERCHANTABILITY  AND FITNESS FOR  A PARTICULAR  PURPOSE ARE    */
/*    DISCLAIMED.  IN  NO EVENT SHALL THE UNIVERSITY  OF TEXAS AT    */
/*    AUSTIN OR CONTRIBUTORS BE  LIABLE FOR ANY DIRECT, INDIRECT,    */
/*    INCIDENTAL,  SPECIAL, EXEMPLARY,  OR  CONSEQUENTIAL DAMAGES    */
/*    (INCLUDING, BUT  NOT LIMITED TO,  PROCUREMENT OF SUBSTITUTE    */
/*    GOODS  OR  SERVICES; LOSS  OF  USE,  DATA,  OR PROFITS;  OR    */
/*    BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON ANY THEORY OF    */
/*    LIABILITY, WHETHER  IN CONTRACT, STRICT  LIABILITY, OR TORT    */
/*    (INCLUDING NEGLIGENCE OR OTHERWISE)  ARISING IN ANY WAY OUT    */
/*    OF  THE  USE OF  THIS  SOFTWARE,  EVEN  IF ADVISED  OF  THE    */
/*    POSSIBILITY OF SUCH DAMAGE.                                    */
/*                                                                   */
/* The views and conclusions contained in the software and           */
/* documentation are those of the authors and should not be          */
/* interpreted as representing official policies, either expressed   */
/* or implied, of Ecole polytechnique federale de Lausanne.          */
/*********************************************************************/

#ifndef ZEROEQ_HTTP_RESPONSE_H
#define ZEROEQ_HTTP_RESPONSE_H

#include <future>
#include <map>
#include <string>

namespace zeroeq
{
namespace http
{

/** @return ready future wrapping the value passed. */
template<typename T>
std::future<T> make_ready_future( const T value )
{
    std::promise<T> promise;
    promise.set_value( value );
    return promise.get_future();
}

/** HTTP headers which can be used in a Response. */
enum class Header
{
    LOCATION,
    RETRY_AFTER,
    LAST_MODIFIED,
    CONTENT_TYPE
};

/** HTTP codes to be used in a Response. */
enum Code
{
    OK = 200,
    CREATED = 201,
    ACCEPTED = 202,
    NO_CONTENT = 204,
    PARTIAL_CONTENT = 206,
    MULTIPLE_CHOICES = 300,
    MOVED_PERMANENTLY = 301,
    MOVED_TEMPORARILY = 302,
    NOT_MODIFIED = 304,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    NOT_SUPPORTED = 405,
    NOT_ACCEPTABLE = 406,
    REQUEST_TIMEOUT = 408,
    PRECONDITION_FAILED = 412,
    UNSATISFIABLE_RANGE = 416,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503,
    SPACE_UNAVAILABLE = 507
};

/** HTTP methods. */
enum class Verb
{
    GET,
    POST,
    PUT,
    PATCH,
    DELETE
};

/**
 * Response to an HTTPRequest.
 */
struct Response
{
    /** HTTP return code. */
    Code code = Code::OK;

    /** Payload to return in a format specified in CONTENT_TYPE header. */
    std::string payload;

    /** HTTP message headers. */
    std::map< Header, std::string > headers;

    /** Default constructor. */
    Response() = default;

    /** Construct a Response with a given return code. */
    Response( const Code code_ ) : code { code_ }{}

    /** Construct a Response with a given return code and payload. */
    Response( const Code code_, const std::string& payload_ )
        : code { code_ }, payload{ payload_ } {}
};

}
}

#endif
