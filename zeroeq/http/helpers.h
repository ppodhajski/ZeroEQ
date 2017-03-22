
/* Copyright (c) 2017, Human Brain Project
 *                     Stefan.Eilemann@epfl.ch
 */

#ifndef ZEROEQ_HTTP_HELPERS_H
#define ZEROEQ_HTTP_HELPERS_H

#include <future>

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
}
}

#endif
