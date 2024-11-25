/*
 *      WeFish File Server
 *
 *  Author: <hoky.guan@tymphany.com>
 *
 */
#ifndef FSERVER_H
#define FSERVER_H

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <memory>
#include <unistd.h>
#include <vector>
#include <cstdlib>
#include <deque>
#include <list>
#include <utility>
#include <mutex>
#include <map>

#include "jsonrpcpp.hpp"
#include "message.hpp"

#define RELEASE_PKG_DIR "./release/"
#define WEFISH_VERSION "R_20241125_1"
#define UPGRADE_FILE_NAME "WeFish"
#define UPGRADE_FILE_SUFFIX ".exe"

using boost::asio::ip::tcp;

typedef std::deque<std::string> MessageQueue;

using ResponseHandler = std::function<void(const boost::system::error_code&, std::string&)>;

class Participant
{
public:
    virtual ~Participant() {}
    virtual void Deliver(const std::string& msg) = 0;
    int account_;
};

typedef std::shared_ptr<Participant> Participant_ptr;

class Group
{
public:
    Group(){}

    void Join(Participant_ptr participant)
    {
        participants_.push_back(participant);
    }

    void Leave(Participant_ptr participant)
    {
        participants_.remove(participant);
    }

    void Deliver(Participant_ptr self_ptr, const std::string& msg)
    {
        for (auto participant: participants_)
            if (self_ptr != participant)
                participant->Deliver(msg);
    }

    void Deliver(int to_account, const std::string& msg)
    {
        for (auto participant: participants_)
            if (to_account == participant->account_)
                participant->Deliver(msg);
    }

private:
    std::list<Participant_ptr> participants_;
};

class FSession : public Participant, public std::enable_shared_from_this<FSession>
{
public:
    FSession(boost::asio::io_context& io_context, tcp::socket socket, Group& group);

    void Start();
    void Deliver(const std::string& msg);
    void processRequest(const jsonrpcpp::request_ptr request, jsonrpcpp::entity_ptr& response, jsonrpcpp::notification_ptr& notification);
    std::string doMessageReceived(const std::string& message);
    void doRead();
    void doWrite();

    void doFileSection(int sequence, std::streampos start, std::streampos sectionSize);

private:
    tcp::socket socket_;
    boost::asio::streambuf streambuf_;
    boost::asio::io_context::strand strand_;
    Group& group_;
    MessageQueue messages_;
    std::string key_;
    std::string upgrade_file_name_;
    std::string upgrade_file_content_;
    std::string upgrade_file_checksum_;
    size_t upgrade_file_size_ = 0;
    size_t data_consume_ = 0;
    std::mutex mutex_;
    std::vector<std::string> v_section_;
};

class FServer
{
public:
    FServer(boost::asio::io_context& io_context, const tcp::endpoint& endpoint);
    ~FServer() = default;

    void doAccept();

private:
    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    Group group_;
};

#endif
