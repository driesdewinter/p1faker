#include "www.h"
#include "config.h"
#include "logf.h"
#include "mutex_protected.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <functional>
#include <memory>
#include <thread>

using namespace www;

namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>
namespace asio = boost::asio;    // from <boost/asio.hpp>
using tcp = asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

using namespace std::literals::chrono_literals;

namespace
{

// Return a reasonable mime type based on the extension of a file.
beast::string_view mime_type(beast::string_view path)
{
    using beast::iequals;
    auto const ext = [&path]
    {
        auto const pos = path.rfind(".");
        if(pos == beast::string_view::npos)
            return beast::string_view{};
        return path.substr(pos);
    }();
    if (iequals(ext, ".html")) return "text/html";
    if (iequals(ext, ".css"))  return "text/css";
    if (iequals(ext, ".js"))   return "text/javascript";
    if (iequals(ext, ".json")) return "application/json";
    if (iequals(ext, ".png"))  return "image/png";
    if (iequals(ext, ".jpg"))  return "image/jpeg";
    return "application/octet-stream";
}

struct ip_parser {
    asio::ip::address operator()(std::string_view text) { return asio::ip::make_address(text); }
};

constexpr const char* http_server = "p1gen/1.0";
config::param<std::string> doc_root{"www.doc_root", "public"};

struct rpc_value
{
    rpc* instance;
    std::function<void(const nlohmann::json&, nlohmann::json&)> handler;
};

struct registry
{
    static auto lock()
    {
        static mutex_protected<registry> instance;
        return instance.lock();
    }

    std::map<rpc::key, rpc_value> rpcs;
};

template<class Body, class Allocator, class ResponseHandler>
void handle_request(http::request<Body, http::basic_fields<Allocator>>&& req, ResponseHandler&& response_handler)
{
    // Returns a bad request response
    auto const bad_request =
    [&](beast::string_view why)
    {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, http_server);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        response_handler(std::move(res));
    };

    // Returns a not found response
    auto const not_found =
    [&](beast::string_view target)
    {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, http_server);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        response_handler(std::move(res));
    };

    // Returns a server error response
    auto const server_error =
    [&](beast::string_view what)
    {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, http_server);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        response_handler(std::move(res));
    };

    static constexpr const char* api_prefix = "/api/";
    if (req.target().starts_with(api_prefix))
    {
        rpc::key key;
             if (req.method() == http::verb::get) key.method = method::get;
        else if (req.method() == http::verb::post) key.method = method::post;
        else return bad_request("Unsupported API method");
        key.name = req.target().substr(std::strlen(api_prefix)).to_string();
        auto reg = registry::lock();
        auto it = reg->rpcs.find(key);
        if (it == reg->rpcs.end())
            return not_found(req.target());

        nlohmann::json in;
        nlohmann::json out;
        if (req[http::field::content_type] == "application/json")
        {
            try
            {
                in = nlohmann::json::parse(req.body());
            }
            catch (nlohmann::json::exception& e)
            {
                return bad_request(e.what());
            }
        }
        else if (not req[http::field::content_type].empty())
        {
            return bad_request("RPCs only understand input of Content-Type \"application/json\"");
        }

        try
        {
            it->second.handler(in, out);
        }
        catch (std::exception& e)
        {
            return bad_request(e.what());
        }

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, http_server);
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());
        res.body() = out.dump();
        res.prepare_payload();
        return response_handler(std::move(res));
    }

    // Make sure we can handle the method
    if (req.method() != http::verb::get && req.method() != http::verb::head)

    // Request path must be absolute and not contain "..".
    if (req.target().empty() || req.target()[0] != '/' || req.target().find("..") != beast::string_view::npos)
        return bad_request("Illegal request-target");

    // Build the path to the requested file
    std::string path = doc_root.get() + req.target().to_string();
    if (req.target().back() == '/')
        path.append("index.html");

    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if (ec == beast::errc::no_such_file_or_directory)
        return not_found(req.target());

    // Handle an unknown error
    if (ec)
        return server_error(ec.message());

    // Cache the size since we need it after the move
    auto const size = body.size();

    if (req.method() == http::verb::head)
    {
        // Respond to HEAD request
        http::response<http::empty_body> res{http::status::ok, req.version()};
        res.set(http::field::server, http_server);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return response_handler(std::move(res));
    }
    else if (req.method() == http::verb::get)
    {
        // Respond to GET request
        http::response<http::file_body> res{
            std::piecewise_construct,
            std::make_tuple(std::move(body)),
            std::make_tuple(http::status::ok, req.version())};
        res.set(http::field::server, http_server);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        response_handler(std::move(res));
    }
    else
    {
        return bad_request("Unknown HTTP-method");
    }
}

// Report a failure
void fail(beast::error_code ec, char const* what)
{
    logferror("%s: %s", what, ec.message());
}

// Handles an HTTP server connection
struct connection : public std::enable_shared_from_this<connection>
{
    connection(tcp::socket&& socket) : m_stream(std::move(socket)) {}

    void run()
    {
        asio::dispatch(m_stream.get_executor(),
                beast::bind_front_handler(&connection::do_read, shared_from_this()));
    }

    void do_read()
    {
        m_req = {};
        m_stream.expires_after(300s);

        http::async_read(m_stream, m_buffer, m_req,
                beast::bind_front_handler(&connection::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t)
    {
        // This means they closed the connection
        if (ec == http::error::end_of_stream)
            return do_close();

        if (ec)
            return fail(ec, "read");

        // Send the response
        handle_request(std::move(m_req), [spconn = shared_from_this()]<typename Response>(Response&& _response) {
            auto spresponse = std::make_shared<Response>(std::move(_response));
            spconn->m_spresponse = spresponse;
            bool keep_alive = spresponse->keep_alive();
            http::async_write(spconn->m_stream, *spresponse,
                    beast::bind_front_handler(&connection::on_write, spconn, keep_alive));
        });
    }

    void on_write(bool keep_alive, beast::error_code ec, std::size_t)
    {
        m_spresponse = {};

        if (ec)
            return fail(ec, "write");

        if (not keep_alive)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return do_close();
        }

        // Read another request
        do_read();
    }

    void do_close()
    {
        // Send a TCP shutdown
        beast::error_code ec;
        m_stream.socket().shutdown(tcp::socket::shutdown_send, ec);
    }
private:
    beast::tcp_stream m_stream;
    beast::flat_buffer m_buffer;
    http::request<http::string_body> m_req;
    std::shared_ptr<void> m_spresponse;
};


// Accepts incoming connections and launches the connections
struct listener : public std::enable_shared_from_this<listener>
{
    listener(asio::io_context& ioc, tcp::endpoint endpoint)
    : m_ioc(ioc)
    , m_acceptor(asio::make_strand(m_ioc))
    {
        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(asio::socket_base::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen(asio::socket_base::max_listen_connections);
    }

    void run()
    {
        do_accept();
    }

private:
    void do_accept()
    {
        // The new connection gets its own strand
        m_acceptor.async_accept(asio::make_strand(m_ioc),
            beast::bind_front_handler(&listener::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec, tcp::socket socket)
    {
        if (ec)
            return fail(ec, "accept");

        std::make_shared<connection>(std::move(socket))->run();

        // Accept another connection
        do_accept();
    }

    asio::io_context& m_ioc;
    tcp::acceptor m_acceptor;
};

config::param<asio::ip::address, ip_parser> bind_address{"www.bind_address", asio::ip::address{}};
config::param<uint16_t> bind_port{"www.bind_port", 80};

struct server
{
    server()
    {
        try
        {
            std::make_shared<listener>(m_ioc, tcp::endpoint{bind_address, bind_port})->run();
            m_thread = std::thread{[&] { m_ioc.run(); }};
        }
        catch (boost::system::system_error& e)
        {
            logferror("Failed to set up www server: %s", e.what());
        }
    }

    ~server()
    {
        m_ioc.stop();
        if (m_thread.joinable()) m_thread.join();
    }

    asio::io_context m_ioc;
    std::thread m_thread;
} _server;

} // anonymous namespace

std::ostream& www::method::operator<<(std::ostream& os, method::type v)
{
    static auto labels = {"GET", "POST"};
    return os << *(labels.begin() + v);
}

std::ostream& www::operator<<(std::ostream& os, rpc::key key)
{
    return os << key.method << " /api/" << key.name;
}

void rpc::init(std::function<void(const nlohmann::json&, nlohmann::json&)> handler)
{
    auto reg = registry::lock();
    auto it = reg->rpcs.find(m_key);
    logfdebug("%s RPC %s", it == reg->rpcs.end() ? "Register" : "Overrule", m_key);
    if (it != reg->rpcs.end()) reg->rpcs.erase(it);
    reg->rpcs.emplace(m_key, rpc_value{this, handler});
}

void rpc::move(rpc& src)
{
    auto reg = registry::lock();
    auto it = reg->rpcs.find(m_key);
    if (it == reg->rpcs.end()) return;
    if (it->second.instance != &src) return;
    it->second.instance = this;
}

rpc::~rpc()
{
    auto reg = registry::lock();
    auto it = reg->rpcs.find(m_key);
    if (it == reg->rpcs.end()) return;
    if (it->second.instance != this) return; // this RPC might have been overruled in the meantime, so no guarantee that we find ourselves.
    logfdebug("Unregister RPC %s", m_key);
    reg->rpcs.erase(it);
}
