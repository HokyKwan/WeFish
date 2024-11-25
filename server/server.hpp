/*
 *         WeFish Server
 *
 *  Author: <hoky.guan@tymphany.com>
 *
 */
#ifndef SERVER_H
#define SERVER_H

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include <iostream>
#include <string>
#include <map>
#include <memory>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <cstdlib>
#include <deque>
#include <list>
#include <utility>

#include "jsonrpcpp.hpp"
#include "message.hpp"
#include "cryptor.h"
#include "sbase.h"


using boost::asio::ip::tcp;

typedef std::deque<std::string> MessageQueue;

using ResponseHandler = std::function<void(const boost::system::error_code&, std::string&)>;

class Participant
{
public:
    virtual ~Participant() {}
    virtual void Deliver(const std::string& msg) = 0;

    int account_;
    std::string name_;
    std::string icon_;
};

typedef std::shared_ptr<Participant> Participant_ptr;

class Group
{
public:
    Group() {}

    void Join(Participant_ptr participant)
    {
        participants_.push_back(participant);
    }

    void Flush(Participant_ptr participant)
    {
        for (auto msg: recentMessages_) {
            participant->Deliver(msg);
        }
    }

    void Leave(Participant_ptr participant)
    {
        participants_.remove(participant);
    }

    //Include Self
    void Deliver(const std::string& msg)
    {
        recentMessages_.push_back(msg);
        while (recentMessages_.size() > max_recent_msgs)
            recentMessages_.pop_front();

        for (auto participant: participants_)
            participant->Deliver(msg);
    }
    //Exclude Self
    void Deliver(Participant_ptr self_ptr, const std::string& msg, NotificationType type)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (type == NOTIFICATION_TYPE_HIS) {
            recentMessages_.push_back(msg);
        }
        while (recentMessages_.size() > max_recent_msgs)
            recentMessages_.pop_front();

        for (auto participant: participants_)
            if (self_ptr != participant)
                participant->Deliver(msg);
    }
    //Specify
    void Deliver(int to_account, const std::string& msg)
    {
        for (auto participant: participants_)
            if (to_account == participant->account_)
                participant->Deliver(msg);
    }

    std::string GetActiveList()
    {
        std::string liststr;
        for (auto participant: participants_)
            liststr += "#W#F#" + std::to_string(participant->account_) + "-W-F-" + participant->name_ + "-W-F-" + participant->icon_ + "#W#F#";
        return liststr;
    }

    void DataUpdate(int account, std::string icon_str)
    {
        if (recentMessages_.size() == 0) return ;

        for (int idx = 0; idx < recentMessages_.size(); ++idx) {
            jsonrpcpp::entity_ptr entity = jsonrpcpp::Parser::do_parse(recentMessages_.at(idx));
            if (entity->is_notification()) {
                jsonrpcpp::notification_ptr notification = std::dynamic_pointer_cast<jsonrpcpp::Notification>(entity);
                int account_from_parse = notification->params().get("Account");
                if (account_from_parse == account) {
                    notification.reset(new jsonrpcpp::Notification(notification->method(), \
                        jsonrpcpp::Parameter("Account", notification->params().get("Account"), "Name", notification->params().get("Name"), "Icon", icon_str, \
                                        "ToAccount", notification->params().get("ToAccount"), "Content", notification->params().get("Content"))));
                    std::unique_lock<std::mutex> lock(mutex_);
                    recentMessages_[idx] = notification->to_json().dump();
                }
            }
        }
        for (auto participant: participants_) {
            if (participant->account_ == account) {
                participant->icon_ = icon_str;
            }
        }
    }

private:
    std::list<Participant_ptr> participants_;
    enum { max_recent_msgs = 100 };
    MessageQueue recentMessages_;
    std::mutex mutex_;
};

class Session : public Participant, public std::enable_shared_from_this<Session>
{
public:
    Session(boost::asio::io_context& io_context, tcp::socket socket, Group& group);

    void Start();
    void Deliver(const std::string& msg);
    void processRequest(const jsonrpcpp::request_ptr request, jsonrpcpp::entity_ptr& response, jsonrpcpp::notification_ptr& notification);
    std::string doMessageReceived(const std::string& message);
    void doRead();
    void doWrite();

    //int IDinGroup_;
    //std::string name_;
private:
    tcp::socket socket_;
    boost::asio::streambuf streambuf_;
    boost::asio::io_context::strand strand_;
    Group& group_;
    MessageQueue messages_;
    std::string key_;
    std::shared_ptr<SBase> sbase_;
};

class Server
{
public:
    Server(boost::asio::io_context& io_context, const tcp::endpoint& endpoint);
    ~Server() = default;

    void doAccept();

private:
    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    Group group_;
};

#endif
