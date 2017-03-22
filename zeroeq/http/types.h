
/* Copyright (c) 2017, Human Brain Project
 *                     Stefan.Eilemann@epfl.ch
 */

#pragma once

#include <zeroeq/types.h>

#include <future>

namespace zeroeq
{
namespace http
{
enum class Method;

/** HTTP PUT callback w/o payload, return reply success. */
using PUTFunc = std::function< bool() > ;

/** HTTP PUT callback w/ JSON payload, return reply success. */
using PUTPayloadFunc = std::function< bool( const std::string& ) >;

/** HTTP GET callback to return JSON reply. */
using GETFunc = std::function< std::string() >;

/**
 * HTTP REST callback with payload, returning a Response future.
 * The string is the request's payload
 */
using RESTFunc = std::function< std::future< Response >( const std::string& ) >;

/**
 * HTTP REST callback for a given path returning a Response future.
 * The first string provides the url path after the registered endpoint:
 * "api/windows/jf321f" -> "jf321f", which is empty in case the path matches the
 * endpoint. The second is the request's payload.
 */
using RESTPathFunc = std::function< std::future< Response >( const std::string&,
                                                         const std::string& ) >;
}
}
