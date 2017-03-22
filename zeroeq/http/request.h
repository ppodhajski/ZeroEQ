/* Copyright (c) 2017, EPFL/Blue Brain Project
 *                     Stefan.Eilemann@epfl.ch
 */

#pragma once

namespace zeroeq
{
namespace http
{
enum class Method
{
    PUT,
    GET,
    POST,
    PATCH,
    DELETE,
    ALL //!< @internal, must be last
};

}
}
