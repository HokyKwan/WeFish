#include "voice.h"

#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace beast = boost::beast;
namespace http = beast::http;


#define API_KEY "kLzZKVQqJoF3edLNRjfebuVB"
#define SECRET_KEY "8UAm78zRmIKb5CbOLvT2tzkpgEHyIn6w"


void Voice::Start(Request& req, ResponseHandler handler)
{
    req_ = req;
    host_ = req_.Get(Request::field::host);
    path_ = req_.Get(Request::field::target);
    content_ = req_.Get(Request::field::body);
    response_handler_ = handler;

    boost::asio::ip::tcp::resolver::query query(host_, "https");
    auto self = shared_from_this();
    resolver_.async_resolve(query,
        [this, self] (const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::results_type endpoints) {
            doResolve(ec, endpoints);
        });
}

void Voice::doResolve(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::results_type endpoints)
{
    if (!ec) {
        auto self = shared_from_this();
        boost::asio::async_connect(
            socket_.lowest_layer(), endpoints,
            [this, self] (const boost::system::error_code& ec, boost::asio::ip::tcp::endpoint) {
                doConnect(ec);
            });
    } else {
        std::string feedback = "{\"Exception\":\"Unresolve\"}";
        response_handler_("unknown", feedback);
    }
}

void Voice::doConnect(const boost::system::error_code& ec)
{
    if (!ec) {
        if (!SSL_set_tlsext_host_name(socket_.native_handle(), host_.c_str())) {
            std::cerr << "Failed to set SNI\n";
            return;
        }

        auto self = shared_from_this();
        socket_.async_handshake(boost::asio::ssl::stream_base::client,
        [this, self] (const boost::system::error_code& ec) {
            if (!ec) {
                req_.Prepare();
                doWrite(ec, req_);
            } else {
                std::string feedback = "{\"Exception\":\"Unhandshake\"}";
                response_handler_("unknown", feedback);
            }
        });
    } else {
        std::string feedback = "{\"Exception\":\"Unconnect\"}";
        response_handler_("unknown", feedback);
    }
}

void Voice::doWrite(const boost::system::error_code& ec, Request &req)
{
    std::ostream request_stream(&request_);
    request_stream << req;
    std::cout << req << std::endl;
    auto self = shared_from_this();
    boost::asio::async_write(socket_, request_,
        [this, self] (const boost::system::error_code& ec, std::size_t bytes_transferred) {
            doReadHeaders(ec);
        });
}

void Voice::doReadHeaders(const boost::system::error_code& ec)
{
    if (!ec) {
        const std::string delimiter = "\r\n\r\n";
        auto self = shared_from_this();
        boost::asio::async_read_until(socket_, response_, delimiter,
            [this, self, delimiter] (const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (!ec) {
                    std::string header(boost::asio::buffers_begin(response_.data()), boost::asio::buffers_end(response_.data()));
                    std::cout << header << "\n";

                    if (header.find("Content-Length: ") != std::string::npos) {
                        std::string feedback = header.substr(bytes_transferred);
                        response_handler_("taskid", feedback);
                        response_.consume(response_.size());
                        return ;
                    } else {
                        response_.consume(response_.size());
                        doReadBodySize(ec);
                    }

                } else {
                    std::cerr << "[Err] Receive Response Header failed: " << ec.message() << "\n";
                }
            });
    } else {
        std::cerr << "Read headers error: " << ec.message() << std::endl;
    }
}

void Voice::doReadBodySize(const boost::system::error_code& ec)
{
    if (!ec) {
        const std::string delimiter = "\n";
        auto self = shared_from_this();
        boost::asio::async_read_until(
            socket_, response_, delimiter,
            [this, self] (const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (!ec) {
                    std::string body(boost::asio::buffers_begin(response_.data()), boost::asio::buffers_end(response_.data()));
                    std::string body_size(body, 0, bytes_transferred - 1);
                    body_size_ = std::stoi(body_size, nullptr, 16);

                    std::cout << "BodySize: " << std::to_string(body_size_) << "\n";
                    response_.consume(bytes_transferred);
                    doReadBody(ec);
                } else {
                    std::cerr << "[Err] Receive Response Body Size failed: " << ec.message() << "\n";
                }
            });
    }
}

void Voice::doReadBody(const boost::system::error_code& ec)
{
    if (!ec) {
        const std::string delimiter = "\r\n";
        auto self = shared_from_this();
        boost::asio::async_read_until(
            socket_, response_, delimiter,
            [this, self] (const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (!ec) {
                    std::string body(boost::asio::buffers_begin(response_.data()), boost::asio::buffers_end(response_.data()));
                    std::cout << "Response: " << body << "\n";
                    response_.consume(response_.size());
                    body_.append(body);

                    cJSON *response_root = cJSON_Parse(body_.c_str());
                    if (response_root) {
                        body_.clear();
                        cJSON *access_token_item = cJSON_GetObjectItem(response_root, "access_token");
                        response_handler_("accesstoken", access_token_item->valuestring);
                    } else {
                        if (body_size_) {
                            doReadBodySize(ec);
                        }
                    }
                } else {
                    std::cerr << "[Err] Receive Response Body failed: " << ec.message() << "\n";
                }
            });
    }
}
