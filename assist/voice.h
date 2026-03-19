#ifndef VOICE_H
#define VOICE_H

#include "message.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <cjson/cJSON.h>
#include <functional>


using ResponseHandler = std::function<void(std::string, std::string)>;

class Voice : public std::enable_shared_from_this<Voice> {
public:
    Voice(boost::asio::io_context& ioc, boost::asio::ssl::context& ctx)
        : resolver_(ioc), socket_(ioc, ctx) {
    }

    ~Voice() = default;

    void Start(Request& req, ResponseHandler handler);

private:
    void doResolve(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::results_type endpoints);
    void doConnect(const boost::system::error_code& ec);
    void doWrite(const boost::system::error_code& ec, Request &req);
    void doReadHeaders(const boost::system::error_code& ec);
    void doReadBodySize(const boost::system::error_code& ec);
    void doReadBody(const boost::system::error_code& ec);

    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;
    boost::asio::streambuf request_;
    boost::asio::streambuf response_;
    Request req_;
    std::string host_;
    std::string path_;
    std::size_t body_size_;
    std::string content_;
    std::string body_ = "";
    ResponseHandler response_handler_;
};

#endif