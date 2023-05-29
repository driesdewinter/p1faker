#ifndef WWW_H_
#define WWW_H_

#include <nlohmann/json.hpp>
#include <functional>

namespace www
{

namespace method
{
enum type
{
    get,
    post
};
std::ostream& operator<<(std::ostream& os, type v);
}

class rpc
{
public:
    struct key
    {
        method::type method;
        std::string name;

        auto operator<=>(const key&) const = default;
    };

    /** Register nullary handler for GET-request. Return value is serialized to JSON in response body. */
    template<typename F>
    static rpc get(std::string name, F&& handler)
    {
        rpc result{key{method::get, name}};
        result.init([handler](const nlohmann::json&, nlohmann::json& out) {
            out = handler();
        });
        return result;
    }

    /**
     * Register unary handler for POST-request.
     * The type of the argument needs to be specified as template argument
     * It is deserialized from JSON found in request body.
     * Return value of handler is ignored, 204 No Content is returned as response. */
    template<typename Request, typename F>
    static rpc post(std::string name, F&& handler)
    {
        rpc result{key{method::post, name}};
        result.init([handler](const nlohmann::json& in, nlohmann::json&) {
            handler(in.get<Request>());
        });
        return result;
    }

    rpc() = default;
    rpc(rpc&& r) : m_key(r.m_key) { move(r); }
    rpc& operator=(rpc&& r) { m_key = r.m_key; move(r); return *this; }
    rpc(const rpc&) = delete;
    rpc& operator=(const rpc&) = delete;
    ~rpc();

private:
    rpc(key k) : m_key{k} {}
    void init(std::function<void(const nlohmann::json&, nlohmann::json&)>);
    void move(rpc& src);
    key m_key;
};
std::ostream& operator<<(std::ostream& os, rpc::key key);

}

#endif /* WWW_H_ */
