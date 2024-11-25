/*
 *         WeFish Server
 *
 *  Author: <hoky.guan@tymphany.com>
 *
 */
#include "server.hpp"

#include <csignal>
#include <chrono>
#include <exception>
#include <system_error>
#include <boost/algorithm/string.hpp>

#include "cryptor.h"


Session::Session(boost::asio::io_context& io_context, tcp::socket socket, Group& group)
    : socket_(std::move(socket)), group_(group), strand_(io_context)
{
    key_ = "ba3483abc1af7e9d0cf2325010ed76d7";
    sbase_ = std::make_shared<SBase>();
    sbase_->Initialise("./wefish.db");
}

void Session::processRequest(const jsonrpcpp::request_ptr request, jsonrpcpp::entity_ptr& response, jsonrpcpp::notification_ptr& notification)
{
    try {
        Json result;
        if (request->id().int_id() == MESSAGE_TYPE_CONTENT) {
            if (request->method() == "Content") {
                notification.reset(new jsonrpcpp::Notification("ContentNotification", \
                    jsonrpcpp::Parameter("Account", request->params().get("Account"), "Name", request->params().get("Name"), "Icon", request->params().get("Icon"), \
                                        "ToAccount", request->params().get("ToAccount"), "Content", request->params().get("Content"))));  
            } else if (request->method() == "PicContent") {
                notification.reset(new jsonrpcpp::Notification("PicContentNotification", \
                    jsonrpcpp::Parameter("Account", request->params().get("Account"), "Name", request->params().get("Name"), "Icon", request->params().get("Icon"), \
                                        "ToAccount", request->params().get("ToAccount"), "Content", request->params().get("Content"))));
            }
        } else if (request->id().int_id() == MESSAGE_TYPE_SETTING) {
            if (request->method() == "SayHello") {
                group_.Join(shared_from_this());
                account_ = request->params().get("Account");
                name_ = request->params().get("Name");
                icon_ = request->params().get("Icon");
                notification.reset(new jsonrpcpp::Notification("OnlineNotification", jsonrpcpp::Parameter("Account", account_, "Name", name_, "Icon", icon_)));
                result["Method"] = "SayHello";
                result["Account"] = account_;
                result["ActiveList"] = group_.GetActiveList();
                response.reset(new jsonrpcpp::Response(*request, result));
            } else if (request->method() == "RegisterRequest") {
                std::string code = sbase_->GetInviteCode();
                if (code.compare(request->params().get("Invitecode")) == 0) {
                    int use_times = sbase_->GetInviteCodeTimes();
                    if (use_times < 30) {
                        int user_id = 522000 + sbase_->Count();
                        std::cout << "New Account Registered, ID: " << std::to_string(user_id) << std::endl;
                        std::string passw = request->params().get("Password");
                        std::string name = request->params().get("Name");
                        std::string icon = request->params().get("Icon");
                        sbase_->Insert(user_id, passw, name, icon);
                        sbase_->UpdateInviteCodeTimes(use_times + 1);
                        result["Method"] = "RegisterRequest";
                        result["StatusCode"] = 0;
                        result["Account"] = std::to_string(user_id);
                        result["Password"] = passw;
                    } else {
                        std::string new_invite_code = sbase_->GenerateInviteCode();
                        std::cout << "Generated New Invite Code: " << new_invite_code << std::endl;
                        sbase_->ChangeInviteCode(new_invite_code);
                        result["Method"] = "RegisterRequest";
                        result["StatusCode"] = 1;
                        result["NewInviteCode"] = new_invite_code;
                    }
                    response.reset(new jsonrpcpp::Response(*request, result));
                } else {
                    result["Method"] = "RegisterRequest";
                    result["StatusCode"] = 2;
                    response.reset(new jsonrpcpp::Response(*request, result));
                }
            } else if (request->method() == "PermissionRequest") {
                int account = request->params().get("Account");
                std::string passw;
                std::string name;
                std::string icon;
                sbase_->ReadByID(account, "passw", &passw);
                if (passw.compare(request->params().get("Password")) == 0) {
                    sbase_->ReadByID(account, "name", &name);
                    sbase_->ReadByID(account, "icon", &icon);
                    result["Method"] = "PermissionRequest";
                    result["StatusCode"] = 0;
                    result["Account"] = account;
                    result["Name"] = name;
                    result["Icon"] = icon;
                } else {
                    result["Method"] = "PermissionRequest";
                    result["StatusCode"] = 1;
                }
                response.reset(new jsonrpcpp::Response(*request, result));
            } else if (request->method() == "SettingRequest") {
                int account = request->params().get("Account");
                int alter = request->params().get("Alter");

                if (alter == 0) {
                    std::string new_passw = request->params().get("NewPassword");
                    std::string passw;
                    sbase_->ReadByID(account, "passw", &passw);
                    if (passw.compare(request->params().get("OldPassword")) == 0) {
                        sbase_->UpdateByID(account, "passw", new_passw);
                        result["Method"] = "SettingRequest";
                        result["StatusCode"] = 0;
                    } else {
                        result["Method"] = "SettingRequest";
                        result["StatusCode"] = 1;
                    }
                    response.reset(new jsonrpcpp::Response(*request, result));
                } else if (alter == 1) {
                    std::string icon = request->params().get("Icon");
                    sbase_->UpdateByID(account, "icon", icon);
                    notification.reset(new jsonrpcpp::Notification("SettingNotification", jsonrpcpp::Parameter("Account", account, "Icon", icon)));
                    std::thread([=](int, std::string) {
                        group_.DataUpdate(account, icon);
                        // std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    }, account, icon).detach();
                }
            }
        } else {
            std::cout << "NULL Process Request\n";
        }
    } catch (const std::exception& e) {
        std::cout << "Server::onMessageReceived exception: " << e.what() << ", message: " << request->to_json().dump() << "\n";
        response.reset(new jsonrpcpp::InternalErrorException(e.what(), request->id()));
    }
}


std::string Session::doMessageReceived(const std::string& message)
{
    jsonrpcpp::entity_ptr entity(nullptr);
    try {
        entity = jsonrpcpp::Parser::do_parse(message);
        if (!entity)
            return "";
    } catch (const jsonrpcpp::ParseErrorException& e) {
        std::cout << e.to_json().dump() << "\nCaused by:" << message << "\n";
        return e.to_json().dump();
    } catch (const std::exception& e) {
        std::cout << e.what() << "\nCaused by:" << message << "\n";
        return jsonrpcpp::ParseErrorException(e.what()).to_json().dump();
    }
    jsonrpcpp::entity_ptr response(nullptr);
    jsonrpcpp::notification_ptr notification(nullptr);
    if (entity->is_request()) {
        auto self(shared_from_this());
        jsonrpcpp::request_ptr request = std::dynamic_pointer_cast<jsonrpcpp::Request>(entity);
        processRequest(request, response, notification);
        if (notification) {
            if (notification->method() == "OnlineNotification") {
                group_.Deliver(self, notification->to_json().dump(), NOTIFICATION_TYPE_NO_HIS);
            } else if (notification->method() == "ContentNotification") {
                if (!(int)notification->params().get("ToAccount")) {
                    group_.Deliver(self, notification->to_json().dump(), NOTIFICATION_TYPE_HIS);
                } else {
                    group_.Deliver((int)notification->params().get("ToAccount"), notification->to_json().dump());
                }
            } else if (notification->method() == "PicContentNotification") {
                if (!(int)notification->params().get("ToAccount")) {
                    group_.Deliver(self, notification->to_json().dump(), NOTIFICATION_TYPE_HIS);
                } else {
                    group_.Deliver((int)notification->params().get("ToAccount"), notification->to_json().dump());
                }
            } else if (notification->method() == "SettingNotification") {
                group_.Deliver(self, notification->to_json().dump(), NOTIFICATION_TYPE_NO_HIS);
            }
        }
        if (response) {
            //std::cout << "Response: " << response->to_json().dump() << "\n";
            return response->to_json().dump();
        }
        return "";
    }
}

void Session::Start()
{
    // Need SayHello to join chat group
    // group_.Join(shared_from_this());
    doRead();
}

void Session::doRead()
{
    auto self(shared_from_this());
    const std::string delimiter = "\n";
    boost::asio::async_read_until(
        socket_, streambuf_, delimiter,
        boost::asio::bind_executor(strand_, [this, self, delimiter](const std::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                auto self = shared_from_this();
                jsonrpcpp::notification_ptr notification(nullptr);
                notification.reset(new jsonrpcpp::Notification("OfflineNotification", jsonrpcpp::Parameter("Name", self->name_, "Account", account_)));
                group_.Deliver(self, notification->to_json().dump(), NOTIFICATION_TYPE_NO_HIS);
                group_.Leave(self);
                return;
            }
            std::string line{buffers_begin(streambuf_.data()), buffers_begin(streambuf_.data()) + bytes_transferred - delimiter.length()};
            if (!line.empty()) {
                //std::cout << "Line: " << line << "\n";
                if (line.find("Cookie: mstshash=hello") != std::string::npos) return; //Avoid attacking
                if (line.back() == '\r')
                    line.resize(line.size() - 1);
                if (!line.empty()) {
                    // Base64 Encrypt
                    // std::string decoded = CBASE64::decode(line);

                    std::string decrypt = AESDecrypt(line, key_);
                    // Remove any characters after }}
                    std::string decoded = decrypt.substr(0, decrypt.find("}}") + 2);

                    //std::cout << "Line: " << decoded << "\n";
                    while ((int)decoded.back() == 15) {
                        decoded.resize(decoded.size() - 1);
                    }

                    std::string response = doMessageReceived(decoded);
                    //For response to client
                    if (!response.empty()) {
                        Deliver(response);
                        //Flush history after return activelist
                        jsonrpcpp::entity_ptr entity(nullptr);
                        entity = jsonrpcpp::Parser::do_parse(response);
                        jsonrpcpp::response_ptr response = std::dynamic_pointer_cast<jsonrpcpp::Response>(entity);
                        if (response->to_json()["result"]["Method"] == "SayHello") {
                            group_.Flush(shared_from_this());
                        }
                    }
                }
            }
            streambuf_.consume(bytes_transferred);
            doRead();
        }));
}

void Session::doWrite()
{
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(messages_.front().data(), messages_.front().length()),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                messages_.pop_front();
                if (!messages_.empty())
                    doWrite();
            } else {
                group_.Leave(shared_from_this());
            }
        });
}


void Session::Deliver(const std::string& msg)
{
    bool write_in_progress = !messages_.empty();
    // std::string encoded = CBASE64::encode(msg);

    std::string encoded = AESEncrypt(msg, key_);
    messages_.push_back(encoded + "\r\n");
    if (!write_in_progress) {
        doWrite();
    }
}

Server::Server(boost::asio::io_context& io_context, const tcp::endpoint& endpoint)
    : io_context_(io_context), acceptor_(io_context, endpoint)
{
    doAccept();
}

void Server::doAccept()
{
    acceptor_.async_accept(
        [this] (boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<Session>(io_context_, std::move(socket), group_)->Start();
            } else {
                std::cout << "Accept error: " << ec.message() << "\n";
            }
            doAccept();
        });
}

int main(int argc, char* argv[])
{
    try
    {
        if (argc < 2)
        {
            std::cerr << "Usage: ./server <Group port>\n";
            return 1;
        }

        boost::asio::io_context io_context;

        std::list<Server> servers;
        for (int i = 1; i < argc; ++i)
        {
            tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[i]));
            servers.emplace_back(io_context, endpoint);
        }

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM, SIGPIPE);
        signals.async_wait([&](const boost::system::error_code& ec, int signal) {
            if (!ec) {
                switch (signal) {
                case SIGINT:
                case SIGTERM:
                    throw std::system_error();
                    break;
                case SIGPIPE:
                    break;
                default:
                    throw std::system_error();
                    break;
                }
            } else {
                std::cout << "Failed to wait for signal, error: " << ec.message() << "\n";
            }
            io_context.stop();
        });

        io_context.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "\nException Occurred\n";
        std::cout << e.what();
    }
    std::cout << "\nWeFish Server terminated.\n";
    return 0;
}


