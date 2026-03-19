#ifndef CONNECTION_H
#define CONNECTION_H

#include "message.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <cjson/cJSON.h>
#include <functional>


using ResponseHandler = std::function<void(std::string, std::string)>;

class Connection {
public:
    Connection(boost::asio::io_context& ioc, boost::asio::ssl::context& ctx)
        : resolver_(ioc), socket_(ioc, ctx) {
        // boost::asio::ssl::context ctx{boost::asio::ssl::context::tlsv12_client};
        // socket_ = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(io_context, ctx);
    }

    ~Connection() = default;

    void Start(std::string content, ResponseHandler handler);

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
    std::string host_;
    std::string path_;
    std::size_t body_size_;
    std::string content_;
    std::string body_ = "";
    ResponseHandler response_handler_;
};

#endif