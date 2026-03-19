#include "connection.h"

#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace beast = boost::beast;
namespace http = beast::http;


#define API_KEY "sk-287508b09f59410288867ae701c4bbdf"

void Connection::Start(std::string content, ResponseHandler handler)
{
    host_ = "api.deepseek.com";
    path_ = "/chat/completions";
    content_ = content;
    response_handler_ = handler;

    boost::asio::ip::tcp::resolver::query query(host_, "https");
    resolver_.async_resolve(query,
        [this] (const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::results_type endpoints) {
            doResolve(ec, endpoints);
        });
}

void Connection::doResolve(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::results_type endpoints)
{
    if (!ec) {
        boost::asio::async_connect(
            socket_.lowest_layer(), endpoints,
            [this] (const boost::system::error_code& ec, boost::asio::ip::tcp::endpoint) {
                doConnect(ec);
            });
    } else {
        std::string feedback = "{\"Exception\":\"Unresolve\"}";
        response_handler_("unknown", feedback);
    }
}

void Connection::doConnect(const boost::system::error_code& ec)
{
    if (!ec) {
        if (!SSL_set_tlsext_host_name(socket_.native_handle(), host_.c_str())) {
            std::cerr << "Failed to set SNI\n";
            return;
        }

        socket_.async_handshake(boost::asio::ssl::stream_base::client,
        [this] (const boost::system::error_code& ec) {
            if (!ec) {
                Request req;
                req.Method(Request::verb::post);
                req.Target(path_);
                req.Version(11);
                req.Set(Request::field::host, host_);
                req.Set(Request::field::authorization, "Bearer " + std::string(API_KEY));
                req.Set(Request::field::content_type, "application/json");
                req.Set(Request::field::content_length, std::to_string(content_.length()));
                req.Body() = content_;
                req.Prepare();
                doWrite(ec, req);
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

void Connection::doWrite(const boost::system::error_code& ec, Request &req)
{
    std::ostream request_stream(&request_);
    request_stream << req;
    // std::cout << req << std::endl;
    boost::asio::async_write(socket_, request_,
        [this] (const boost::system::error_code& ec, std::size_t bytes_transferred) {
            doReadHeaders(ec);
        });
}

void Connection::doReadHeaders(const boost::system::error_code& ec)
{
    if (!ec) {
        const std::string delimiter = "\r\n\r\n";
        boost::asio::async_read_until(socket_, response_, delimiter,
            [this, delimiter] (const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (!ec) {
                    std::istream response_stream(&response_);
                    std::string http_version;
                    unsigned int status_code;
                    std::string status_message;

                    response_stream >> http_version;
                    response_stream >> status_code;
                    std::getline(response_stream, status_message);
                    // std::cout << http_version << " " << status_code << " " << status_message << std::endl;
                    // std::string header(boost::asio::buffers_begin(response_buffer_.data()), boost::asio::buffers_end(response_buffer_.data()));
                    // std::cout << header << "\n";
                    response_.consume(response_.size());

                    doReadBodySize(ec);
                } else {
                    std::cerr << "[Err] Receive Response Header failed: " << ec.message() << "\n";
                }
            });
    } else {
        std::cerr << "Read headers error: " << ec.message() << std::endl;
    }
}

void Connection::doReadBodySize(const boost::system::error_code& ec)
{
    if (!ec) {
        const std::string delimiter = "\n";
        boost::asio::async_read_until(
            socket_, response_, delimiter,
            [this] (const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (!ec) {
                    std::string body(boost::asio::buffers_begin(response_.data()), boost::asio::buffers_end(response_.data()));
                    std::string body_size(body, 0, bytes_transferred - 1);
                    body_size_ = std::stoi(body_size, nullptr, 16);

                    // std::cout << "BodySize: " << std::to_string(body_size_) << "\n";
                    response_.consume(bytes_transferred);
                    doReadBody(ec);
                } else {
                    std::cerr << "[Err] Receive Response Body Size failed: " << ec.message() << "\n";
                }
            });
    }
}

void Connection::doReadBody(const boost::system::error_code& ec)
{
    if (!ec) {
        const std::string delimiter = "\r\n";
        boost::asio::async_read_until(
            socket_, response_, delimiter,
            [this] (const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (!ec) {
                    std::string body(boost::asio::buffers_begin(response_.data()), boost::asio::buffers_end(response_.data()));
                    // std::cout << "Response: " << body << "\n";
                    response_.consume(response_.size());
                    body_.append(body);

                    cJSON *response_root = cJSON_Parse(body_.c_str());
                    if (response_root) {
                        body_.clear();
                        cJSON *choices_item = cJSON_GetObjectItem(response_root, "choices");
                        cJSON *array = cJSON_GetArrayItem(choices_item, 0);
                        cJSON *message_item = cJSON_GetObjectItem(array, "message");
                        cJSON *content_item = cJSON_GetObjectItem(message_item, "content");
                        response_handler_("response", content_item->valuestring);
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