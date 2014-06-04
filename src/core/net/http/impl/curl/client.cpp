/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Thomas Voß <thomas.voss@canonical.com>
 */

#include "client.h"
#include "curl.h"
#include "request.h"

#include <core/net/http/method.h>

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/archive/iterators/ostream_iterator.hpp>

namespace net = core::net;
namespace http = core::net::http;
namespace bai = boost::archive::iterators;

namespace
{

const std::string BASE64_PADDING[] = { "", "==", "=" };

}

http::impl::curl::Client::Client()
{
    multi.set_option(::curl::multi::Option::pipelining, ::curl::easy::enable);
}

std::string http::impl::curl::Client::url_escape(const std::string& s) const
{
    return ::curl::native::escape(s);
}

std::string http::impl::curl::Client::base64_encode(const std::string& s) const
{
    std::stringstream os;

    // convert binary values to base64 characters
    typedef bai::base64_from_binary
    // retrieve 6 bit integers from a sequence of 8 bit bytes
    <bai::transform_width<const char *, 6, 8> > base64_enc;

    std::copy(base64_enc(s.c_str()), base64_enc(s.c_str() + s.size()),
            std::ostream_iterator<char>(os));

    os << BASE64_PADDING[s.size() % 3];

    return os.str();
}

std::string http::impl::curl::Client::base64_decode(const std::string& s) const
{
    std::stringstream os;

    typedef bai::transform_width<bai::binary_from_base64<const char *>, 8, 6> base64_dec;

    unsigned int size = s.size();

    // Remove the padding characters
    // See: https://svn.boost.org/trac/boost/ticket/5629
    if (size && s[size - 1] == '=') {
        --size;
        if (size && s[size - 1] == '=')
            --size;
    }
    if (size == 0)
        return std::string();

    std::copy(base64_dec(s.data()), base64_dec(s.data() + size),
            std::ostream_iterator<char>(os));

    return os.str();
}

core::net::http::Client::Timings http::impl::curl::Client::timings()
{
    return multi.timings();
}

void http::impl::curl::Client::run()
{
    multi.run();
}

void http::impl::curl::Client::stop()
{
    multi.stop();
}

std::shared_ptr<http::Request> http::impl::curl::Client::head(const http::Request::Configuration& configuration)
{
    ::curl::easy::Handle handle;
    handle.method(http::Method::head)
            .url(configuration.uri.c_str())
            .header(configuration.header);

    //const long value = configuration.ssl.verify_host ? ::curl::easy::enable : ::curl::easy::disable;
    handle.set_option(::curl::Option::ssl_verify_host,
                      configuration.ssl.verify_host ? ::curl::easy::enable_ssl_host_verification : ::curl::easy::disable);
    handle.set_option(::curl::Option::ssl_verify_peer,
                      configuration.ssl.verify_peer ? ::curl::easy::enable : ::curl::easy::disable);

    if (configuration.authentication_handler.for_http)
    {
        auto credentials = configuration.authentication_handler.for_http(configuration.uri);
        handle.http_credentials(credentials.username, credentials.password);
    }

    return std::shared_ptr<http::Request>{new http::impl::curl::Request{multi, handle}};
}

std::shared_ptr<http::Request> http::impl::curl::Client::get(const http::Request::Configuration& configuration)
{
    ::curl::easy::Handle handle;
    handle.method(http::Method::get)
          .url(configuration.uri.c_str())
          .header(configuration.header);

    handle.set_option(::curl::Option::ssl_verify_host,
                      configuration.ssl.verify_host ? ::curl::easy::enable_ssl_host_verification : ::curl::easy::disable);
    handle.set_option(::curl::Option::ssl_verify_peer,
                      configuration.ssl.verify_peer ? ::curl::easy::enable : ::curl::easy::disable);

    if (configuration.authentication_handler.for_http)
    {
        auto credentials = configuration.authentication_handler.for_http(configuration.uri);
        handle.http_credentials(credentials.username, credentials.password);
    }

    return std::shared_ptr<http::Request>{new http::impl::curl::Request{multi, handle}};
}

std::shared_ptr<http::Request> http::impl::curl::Client::post(
        const Request::Configuration& configuration,
        const std::string& payload,
        const std::string& ct)
{
    ::curl::easy::Handle handle;
    handle.method(http::Method::post)
            .url(configuration.uri.c_str())
            .header(configuration.header)
            .post_data(payload.c_str(), ct);

    handle.set_option(::curl::Option::ssl_verify_host,
                      configuration.ssl.verify_host ? ::curl::easy::enable_ssl_host_verification : ::curl::easy::disable);
    handle.set_option(::curl::Option::ssl_verify_peer,
                      configuration.ssl.verify_peer ? ::curl::easy::enable : ::curl::easy::disable);

    if (configuration.authentication_handler.for_http)
    {
        auto credentials = configuration.authentication_handler.for_http(configuration.uri);
        handle.http_credentials(credentials.username, credentials.password);
    }

    return std::shared_ptr<http::Request>{new http::impl::curl::Request{multi, handle}};
}

std::shared_ptr<http::Request> http::impl::curl::Client::put(
        const Request::Configuration& configuration,
        std::istream& payload,
        std::size_t size)
{
    ::curl::easy::Handle handle;
    handle.method(http::Method::put)
            .url(configuration.uri.c_str())
            .header(configuration.header)
            .on_read_data([&payload, size](void* dest, std::size_t /*in_size*/, std::size_t /*nmemb*/)
            {
                auto result = payload.readsome(static_cast<char*>(dest), size);
                return result;
            }, size);

    handle.set_option(::curl::Option::ssl_verify_host,
                      configuration.ssl.verify_host ? ::curl::easy::enable_ssl_host_verification : ::curl::easy::disable);
    handle.set_option(::curl::Option::ssl_verify_peer,
                      configuration.ssl.verify_peer ? ::curl::easy::enable : ::curl::easy::disable);

    if (configuration.authentication_handler.for_http)
    {
        auto credentials = configuration.authentication_handler.for_http(configuration.uri);
        handle.http_credentials(credentials.username, credentials.password);
    }

    return std::shared_ptr<http::Request>{new http::impl::curl::Request{multi, handle}};
}

std::shared_ptr<http::Client> http::make_client()
{
    return std::make_shared<http::impl::curl::Client>();
}
